#pragma once

// Mock of image_embedder.h for builds without AI features.
// Same public interface as the real classes, but with no onnxruntime/opencv
// dependency. Every image embedding operation fails cleanly.

#include <memory>
#include <vector>
#include <string>
#include "text_embedder_remote.h"  // embedding_res_t
#include "ai_mock_common.h"

// Forward declaration only: lets the (real, still-compiled) embedder manager
// pass session/env handles to the mock CLIP image embedder constructor without
// pulling in onnxruntime.
namespace Ort { class Session; class Env; }

enum class ImageEmbedderType {
    clip
};

class ImageEmbedder {
    public:
        virtual embedding_res_t embed(const std::string& image_encoded) = 0;
        virtual std::vector<embedding_res_t> embed_documents(const std::vector<std::string>& inputs) = 0;
        virtual ~ImageEmbedder() = default;
        virtual ImageEmbedderType get_image_embedder_type() = 0;
};

class CLIPImageEmbedder : public ImageEmbedder {
    public:
        CLIPImageEmbedder(const std::shared_ptr<Ort::Session>& /*session*/, const std::shared_ptr<Ort::Env>& /*env*/,
                          const std::string& /*model_path*/) {}
        embedding_res_t embed(const std::string& /*image_encoded*/) override {
            return ai_disabled::embedding_res("Image embedding is disabled in this build (built without AI features).");
        }
        std::vector<embedding_res_t> embed_documents(const std::vector<std::string>& inputs) override {
            return std::vector<embedding_res_t>(inputs.size(),
                ai_disabled::embedding_res("Image embedding is disabled in this build (built without AI features)."));
        }
        ImageEmbedderType get_image_embedder_type() override { return ImageEmbedderType::clip; }
};
