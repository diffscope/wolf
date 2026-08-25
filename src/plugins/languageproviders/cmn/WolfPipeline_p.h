#ifndef WOLF_WOLFPIPELINE_P_H
#define WOLF_WOLFPIPELINE_P_H

#include <memory>
#include <vector>

#include <synthrt/Core/ContribSpec.h>

namespace wolf {

    std::unique_ptr<srt::ContribImportValidator> createWolfImportValidator();

    srt::Expected<std::vector<std::unique_ptr<srt::ContribSpecExtension>>>
        createWolfPipelineExtensions(srt::ContribSpec &spec);

}

#endif // WOLF_WOLFPIPELINE_P_H
