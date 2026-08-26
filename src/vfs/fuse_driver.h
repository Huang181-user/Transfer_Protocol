#pragma once
#include <string>

class FuseDriver {
public:
    static int start_fuse(const std::string& mountpoint, const std::string& remote_base, bool use_kcp);
};
