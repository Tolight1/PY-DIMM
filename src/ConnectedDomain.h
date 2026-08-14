#pragma once

#include <algorithm>

namespace ConnectedDomain {

constexpr int kFourConnectivity = 4;
constexpr int kEightConnectivity = 8;
constexpr int kDefaultConnectivity = kEightConnectivity;
constexpr int kMinimumComponentArea = 9;

inline bool isValidConnectivity(int connectivity)
{
    return connectivity == kFourConnectivity ||
           connectivity == kEightConnectivity;
}

inline int sanitizeConnectivity(int connectivity)
{
    return connectivity == kFourConnectivity ? kFourConnectivity
                                               : kEightConnectivity;
}

} // namespace ConnectedDomain
