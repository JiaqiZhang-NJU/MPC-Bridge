// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MatroskaSubtitleReader.h"
#include "Subtitles/STS.h"
#include "Subtitles/RenderedHdmvSubtitle.h"

namespace Bridge {
// Prepare only a successfully extracted track. Null means invalid/unsupported
// data; callers retain the previous source rather than publishing a partial one.
std::shared_ptr<CSimpleTextSubtitle> MakeMatroskaTextSource(const MatroskaSubtitleTrack& track);
std::shared_ptr<CSimpleTextSubtitle> MakeMatroskaTextSource(const MatroskaSubtitleTrack& track, const std::atomic<bool>& cancelled);
CComPtr<CRenderedHdmvSubtitle> MakeMatroskaPictureSource(const MatroskaSubtitleTrack& track, CCritSec* lock);
CComPtr<CRenderedHdmvSubtitle> MakeMatroskaPictureSource(const MatroskaSubtitleTrack& track, CCritSec* lock, const std::atomic<bool>& cancelled);
}
