#pragma once

#include <net.hpp>

namespace StreamMii
{

extern Net::Compression gCompression;

bool InitThread();
void ShutdownThread();

}