#include "LuaSandbox.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <utility>

#include <lua.hpp>
#include <luajit.h>

#include <stdcorelib/path.h>

#include <wolf/Support/Files.h>
#include <wolf/Support/Utf8.h>

namespace wolf::lua {

    namespace {

        /// Restores the stack top recorded at construction when the scope exits by any path.
        class StackGuard {
        public:
            explicit StackGuard(lua_State *state) : m_state(state), m_top(lua_gettop(state)) {
            }

            ~StackGuard() {
                lua_settop(m_state, m_top);
            }

            StackGuard(const StackGuard &) = delete;
            StackGuard &operator=(const StackGuard &) = delete;

        private:
            lua_State *m_state;
            int m_top;
        };

        std::string errorText(lua_State *state) {
            const auto *message = lua_tostring(state, -1);
            return message == nullptr ? std::string("unknown error") : std::string(message);
        }

        void encode(std::uint32_t code, std::string &out) {
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        }

        // The interpreter is LuaJIT, which implements Lua 5.1 and therefore has no utf8 library.
        // Scripts that process non-ASCII text require one, and a library published under the
        // standard name must provide the complete standard interface rather than a partial
        // imitation.

        int utf8Char(lua_State *state) {
            std::string out;
            const auto count = lua_gettop(state);
            for (int i = 1; i <= count; ++i) {
                encode(static_cast<std::uint32_t>(luaL_checkinteger(state, i)), out);
            }
            lua_pushlstring(state, out.data(), out.size());
            return 1;
        }

        int utf8Len(lua_State *state) {
            std::size_t size = 0;
            const auto *text = luaL_checklstring(state, 1, &size);
            std::size_t position = 0;
            lua_Integer length = 0;
            while (position < size) {
                std::uint32_t code = 0;
                const auto step = decodeUtf8(text + position, size - position, code);
                if (step == 0) {
                    lua_pushnil(state);
                    lua_pushinteger(state, static_cast<lua_Integer>(position + 1));
                    return 2;
                }
                position += step;
                ++length;
            }
            lua_pushinteger(state, length);
            return 1;
        }

        int utf8CodePoint(lua_State *state) {
            std::size_t size = 0;
            const auto *text = luaL_checklstring(state, 1, &size);
            const auto from = static_cast<std::size_t>(luaL_optinteger(state, 2, 1));
            const auto to = static_cast<std::size_t>(luaL_optinteger(state, 3, lua_Integer(from)));
            if (from < 1 || to > size) {
                return luaL_error(state, "out of bounds");
            }
            int produced = 0;
            std::size_t position = from - 1;
            while (position < to) {
                std::uint32_t code = 0;
                const auto step = decodeUtf8(text + position, size - position, code);
                if (step == 0) {
                    return luaL_error(state, "invalid UTF-8 code");
                }
                lua_pushinteger(state, static_cast<lua_Integer>(code));
                ++produced;
                position += step;
            }
            return produced;
        }

        int utf8Offset(lua_State *state) {
            std::size_t size = 0;
            const auto *text = luaL_checklstring(state, 1, &size);
            auto wanted = luaL_checkinteger(state, 2);
            auto position = static_cast<std::size_t>(
                luaL_optinteger(state, 3, wanted >= 0 ? 1 : lua_Integer(size) + 1));
            if (position < 1 || position > size + 1) {
                return luaL_error(state, "position out of bounds");
            }
            std::size_t index = position - 1;
            const auto isContinuation = [&](std::size_t at) {
                return at < size && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80;
            };
            if (wanted > 0) {
                --wanted;
                while (wanted > 0 && index < size) {
                    ++index;
                    while (isContinuation(index)) {
                        ++index;
                    }
                    --wanted;
                }
                if (wanted > 0) {
                    lua_pushnil(state);
                    return 1;
                }
            } else {
                while (wanted < 0 && index > 0) {
                    --index;
                    while (index > 0 && isContinuation(index)) {
                        --index;
                    }
                    ++wanted;
                }
                if (wanted < 0) {
                    lua_pushnil(state);
                    return 1;
                }
            }
            lua_pushinteger(state, static_cast<lua_Integer>(index + 1));
            return 1;
        }

        int utf8Next(lua_State *state) {
            std::size_t size = 0;
            const auto *text = luaL_checklstring(state, 1, &size);
            auto position = static_cast<std::size_t>(luaL_checkinteger(state, 2));
            if (position > 0) {
                std::uint32_t skipped = 0;
                position += decodeUtf8(text + position - 1, size - (position - 1), skipped) - 1;
            }
            if (position >= size) {
                return 0;
            }
            std::uint32_t code = 0;
            const auto step = decodeUtf8(text + position, size - position, code);
            if (step == 0) {
                return luaL_error(state, "invalid UTF-8 code");
            }
            lua_pushinteger(state, static_cast<lua_Integer>(position + 1));
            lua_pushinteger(state, static_cast<lua_Integer>(code));
            return 2;
        }

        int utf8Codes(lua_State *state) {
            luaL_checkstring(state, 1);
            lua_pushcfunction(state, utf8Next);
            lua_pushvalue(state, 1);
            lua_pushinteger(state, 0);
            return 3;
        }

        void openUtf8(lua_State *state) {
            lua_newtable(state);
            const luaL_Reg entries[] = {
                {"char",      utf8Char     },
                {"codepoint", utf8CodePoint},
                {"codes",     utf8Codes    },
                {"len",       utf8Len      },
                {"offset",    utf8Offset   },
                {nullptr,     nullptr      },
            };
            for (const auto *entry = entries; entry->name != nullptr; ++entry) {
                lua_pushcfunction(state, entry->func);
                lua_setfield(state, -2, entry->name);
            }
            lua_pushliteral(state, "[\0-\x7F\xC2-\xFD][\x80-\xBF]*");
            lua_setfield(state, -2, "charpattern");
            lua_setglobal(state, "utf8");
        }

        /// Opens the standard libraries, removes every facility that reaches outside the process
        /// or loads additional code, and installs the utf8 library.
        void sandbox(lua_State *state) {
            luaL_openlibs(state);
            static const char *const removed[] = {
                "io",
                "os",
                "debug",
                "package",
                "require",
                "module",
                "dofile",
                "loadfile",
                "load",
                "loadstring",
                "collectgarbage",
                // jit.on() would re-enable the compiler, and the interrupt hook stops working
                // once the compiler is enabled. A script must not be able to disable its own
                // interruption.
                "jit",
            };
            for (const auto *name : removed) {
                lua_pushnil(state);
                lua_setglobal(state, name);
            }
            openUtf8(state);
        }

    }

    class Sandbox::Impl {
    public:
        ~Impl() {
            if (state != nullptr) {
                lua_close(state);
            }
        }

        lua_State *state = nullptr;
        std::atomic_bool interrupted{false};
    };

    namespace {

        /// Registry key under which each state stores the address of its interrupt flag. The
        /// address of this variable is unique to this translation unit, so no other registry
        /// entry can collide with the key.
        char interruptKey = 0;

        /// Runs every HOOK_INTERVAL instructions and raises an error that ends the call if the
        /// interrupt flag is set.
        void interruptHook(lua_State *state, lua_Debug *) {
            lua_pushlightuserdata(state, &interruptKey);
            lua_rawget(state, LUA_REGISTRYINDEX);
            auto *flag = static_cast<std::atomic_bool *>(lua_touserdata(state, -1));
            lua_pop(state, 1);
            if (flag != nullptr && flag->load(std::memory_order_acquire)) {
                luaL_error(state, "the conversion was stopped");
            }
        }

        /// Number of instructions between hook invocations. The interval is small enough for a
        /// prompt response to an interrupt and large enough to keep the overhead negligible.
        constexpr int HOOK_INTERVAL = 10000;

        /// Runs a prepared call under the interrupt hook.
        int guardedCall(lua_State *state, int arguments, int results) {
            lua_sethook(state, interruptHook, LUA_MASKCOUNT, HOOK_INTERVAL);
            const auto status = lua_pcall(state, arguments, results, 0);
            lua_sethook(state, nullptr, 0, 0);
            return status;
        }

    }

    Sandbox::Sandbox() : _impl(std::make_unique<Impl>()) {
    }

    Sandbox::~Sandbox() = default;

    void Sandbox::interrupt() noexcept {
        _impl->interrupted.store(true, std::memory_order_release);
    }

    void Sandbox::resume() noexcept {
        _impl->interrupted.store(false, std::memory_order_release);
    }

    srt::Expected<std::unique_ptr<Sandbox>> Sandbox::create(const std::string &source,
                                                            const std::string &chunkName) {
        auto sandboxed = std::unique_ptr<Sandbox>(new Sandbox());
        sandboxed->_impl->state = luaL_newstate();
        if (sandboxed->_impl->state == nullptr) {
            return srt::Error(srt::Error::FeatureNotSupported, "cannot create a Lua interpreter");
        }
        auto *state = sandboxed->_impl->state;
        sandbox(state);

        // The interrupt below is a count hook, and LuaJIT does not check hooks inside compiled
        // traces: a loop that runs long enough to be compiled is exactly the loop that can no
        // longer be stopped. This behavior was measured: with the compiler enabled, a bare
        // `while true do end` runs indefinitely with the hook installed.
        //
        // The compiler therefore stays disabled. These scripts perform a few string operations
        // per word, for which the cost of interpretation is negligible, and the ability to stop a
        // runaway package is worth considerably more. The implementation from which this code is
        // ported installed the same hook without disabling the compiler, so its cancellation did
        // not work either.
        //
        // The mode is set after the libraries are opened, because opening them also opens the
        // jit library, which re-enables the compiler.
        luaJIT_setmode(state, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);

        lua_pushlightuserdata(state, &interruptKey);
        lua_pushlightuserdata(state, &sandboxed->_impl->interrupted);
        lua_rawset(state, LUA_REGISTRYINDEX);

        if (luaL_loadbuffer(state, source.data(), source.size(), chunkName.c_str()) != 0) {
            auto message = errorText(state);
            return srt::Error(srt::Error::InvalidFormat, "the script does not compile: " + message);
        }
        if (guardedCall(state, 0, 0) != 0) {
            auto message = errorText(state);
            return srt::Error(srt::Error::InvalidFormat, "the script failed to run: " + message);
        }
        return sandboxed;
    }

    bool Sandbox::hasFunction(const std::string &name) const {
        StackGuard guard(_impl->state);
        lua_getglobal(_impl->state, name.c_str());
        return lua_isfunction(_impl->state, -1);
    }

    srt::Expected<std::vector<std::string>>
        Sandbox::callForStrings(const std::string &name, const std::string &argument) const {
        auto *state = _impl->state;
        StackGuard guard(state);
        lua_getglobal(state, name.c_str());
        lua_pushlstring(state, argument.data(), argument.size());
        if (guardedCall(state, 1, 1) != 0) {
            return srt::Error(srt::Error::InvalidFormat, name + " failed: " + errorText(state));
        }
        if (!lua_istable(state, -1)) {
            return srt::Error(srt::Error::InvalidFormat, name + " must return a table");
        }
        const auto length = lua_objlen(state, -1);
        std::vector<std::string> result;
        result.reserve(length);
        for (std::size_t i = 1; i <= length; ++i) {
            lua_rawgeti(state, -1, static_cast<int>(i));
            if (lua_type(state, -1) != LUA_TSTRING) {
                return srt::Error(srt::Error::InvalidFormat,
                                  name + " returned a non-string at index " + std::to_string(i));
            }
            std::size_t size = 0;
            const auto *text = lua_tolstring(state, -1, &size);
            result.emplace_back(text, size);
            lua_pop(state, 1);
        }
        return result;
    }

    srt::Expected<std::vector<bool>>
        Sandbox::callForFlags(const std::string &name,
                              const std::vector<std::string> &argument) const {
        auto *state = _impl->state;
        StackGuard guard(state);
        lua_getglobal(state, name.c_str());
        lua_createtable(state, static_cast<int>(argument.size()), 0);
        for (std::size_t i = 0; i < argument.size(); ++i) {
            lua_pushlstring(state, argument[i].data(), argument[i].size());
            lua_rawseti(state, -2, static_cast<int>(i + 1));
        }
        if (guardedCall(state, 1, 1) != 0) {
            return srt::Error(srt::Error::InvalidFormat, name + " failed: " + errorText(state));
        }
        if (!lua_istable(state, -1)) {
            return srt::Error(srt::Error::InvalidFormat, name + " must return a table");
        }
        const auto length = lua_objlen(state, -1);
        if (length != argument.size()) {
            // The contract pairs each flag with one phoneme, so a shorter table would silently
            // leave the trailing phonemes unmarked and a longer table would carry flags without
            // phonemes.
            return srt::Error(srt::Error::InvalidFormat,
                              name + " returned " + std::to_string(length) + " flags for " +
                                  std::to_string(argument.size()) + " phonemes");
        }
        std::vector<bool> result;
        result.reserve(length);
        for (std::size_t i = 1; i <= length; ++i) {
            lua_rawgeti(state, -1, static_cast<int>(i));
            if (!lua_isboolean(state, -1)) {
                return srt::Error(srt::Error::InvalidFormat,
                                  name + " returned a non-boolean at index " + std::to_string(i));
            }
            result.push_back(lua_toboolean(state, -1) != 0);
            lua_pop(state, 1);
        }
        return result;
    }

    srt::Expected<std::string> readScript(const std::filesystem::path &path) {
        std::ifstream file;
        if (auto opened = openForReading(file, path, "script", std::ios::binary); !opened) {
            return opened.takeError();
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        auto source = buffer.str();
        if (source.empty()) {
            return srt::Error(srt::Error::InvalidFormat,
                              "the script is empty: " + stdc::path::to_utf8(path));
        }
        return source;
    }

}
