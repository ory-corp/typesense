#pragma once

// Shared helpers for the AI mock headers (used when TYPESENSE_ENABLE_AI is not
// defined). They let the engine link and run without onnxruntime/opencv/whisper
// while every AI operation fails with a clear, uniform error.

#include "text_embedder_remote.h"  // embedding_res_t (onnx-free)

namespace ai_disabled {
    inline embedding_res_t embedding_res(
            const char* message = "Embedding is disabled in this build (built without AI features).") {
        nlohmann::json error;
        error["error"] = message;
        return embedding_res_t(400, error);
    }
}
