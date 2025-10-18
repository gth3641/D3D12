#pragma once
#include <string>
#include <vector>
#include "Util/Util.h"

struct SponzaSubmeshCPU {
    std::vector<Vtx> vertices;
    std::vector<uint32_t> indices;   // 32-bit ¿Œµ¶Ω∫ ±«¿Â
    std::string albedoPath;          // map_Kd
};

bool LoadSponzaOBJ(const std::string& objPath,
    const std::string& mtlBaseDir,
    std::vector<SponzaSubmeshCPU>& out);