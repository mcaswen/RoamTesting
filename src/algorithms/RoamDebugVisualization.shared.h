#ifndef PARALLEL_ROAM_DEBUG_VISUALIZATION_SHARED_H
#define PARALLEL_ROAM_DEBUG_VISUALIZATION_SHARED_H

// This macro-only header is consumed by both C++ and HLSL so every terrain path
// uses the same split/merge event colors.
#define ROAM_DEBUG_SPLIT_RED 0.95
#define ROAM_DEBUG_SPLIT_GREEN 0.08
#define ROAM_DEBUG_SPLIT_BLUE 0.06

#define ROAM_DEBUG_MERGE_RED 0.10
#define ROAM_DEBUG_MERGE_GREEN 0.88
#define ROAM_DEBUG_MERGE_BLUE 0.18

#endif
