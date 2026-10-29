// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Bridge {
struct TranslationCue {
    std::string id, text; // UTF-8 plain reading text; identity assigned by source
    int64_t startMs = 0, endMs = 0;
};
struct TranslationService {
    std::string endpoint, model, targetLanguage = "zh-Hans";
};
struct TranslationBatchResult {
    std::vector<TranslationCue> cues;
    int64_t inputTokens = -1, outputTokens = -1; // unavailable is not zero
};
bool ValidSubtitleTranslationService(const TranslationService& service);
// No credentials in this model, request body or cache identity. Caller supplies
// authorization separately to the transport. These helpers perform no network IO.
bool BuildSubtitleTranslationRequest(const TranslationService& service, const std::vector<TranslationCue>& cues,
    std::string& body, std::string& error);
// Atomic: output is untouched on malformed, truncated, refused or misaligned
// responses. Reordered IDs are reconciled; missing/extra/duplicate IDs fail.
bool ParseSubtitleTranslationResponse(const std::vector<TranslationCue>& source, const std::string& response,
    TranslationBatchResult& output, std::string& error);
// Canonical key material, to be hashed by persistent storage. Includes source
// identity/revision, full text/timings, service/model/language and prompt policy.
std::string SubtitleTranslationCacheMaterial(const TranslationService& service,
    const std::string& sourceIdentity, const std::string& revision, const std::vector<TranslationCue>& cues);
}
