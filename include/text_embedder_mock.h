#pragma once

// Mock of text_embedder.h for builds without AI features.
// Same public interface as the real TextEmbedder, but with no onnxruntime /
// sentencepiece dependency. Every embedding operation fails cleanly.

#include <vector>
#include <memory>
#include <string>
#include "option.h"
#include "text_embedder_tokenizer.h"  // TokenizerType (resolves to the mock)
#include "text_embedder_remote.h"     // embedding_res_t
#include "ai_mock_common.h"

// Forward declaration only: keeps get_session()/get_env() signatures intact for
// the (real, still-compiled) embedder manager without pulling in onnxruntime.
namespace Ort { class Session; class Env; }

class TextEmbedder {
    public:
        TextEmbedder(const std::string& /*model_path*/, const bool /*is_public_model*/) {}
        TextEmbedder(const nlohmann::json& /*model_config*/, size_t num_dims, const bool /*has_custom_dims*/ = false)
            : num_dim(num_dims) {}
        ~TextEmbedder() {}

        embedding_res_t embed_query(const std::string& /*text*/, const size_t /*remote_embedder_timeout_ms*/ = 30000,
                                    const size_t /*remote_embedding_num_tries*/ = 2) {
            return ai_disabled::embedding_res("Text embedding is disabled in this build (built without AI features).");
        }
        std::vector<embedding_res_t> embed_documents(const std::vector<std::string>& inputs,
                                                     const size_t /*remote_embedding_batch_size*/ = 200,
                                                     const size_t /*remote_embedding_timeout_ms*/ = 60000,
                                                     const size_t /*remote_embedding_num_tries*/ = 2) {
            return std::vector<embedding_res_t>(inputs.size(),
                ai_disabled::embedding_res("Text embedding is disabled in this build (built without AI features)."));
        }
        const std::string& get_vocab_file_name() const { return vocab_file_name; }
        const size_t get_num_dim() const { return num_dim; }
        bool is_remote() { return false; }
        Option<bool> validate() {
            return Option<bool>(400, "Embedding models are disabled in this build (built without AI features).");
        }
        std::shared_ptr<Ort::Session> get_session() { return nullptr; }
        std::shared_ptr<Ort::Env> get_env() { return nullptr; }
        const TokenizerType get_tokenizer_type() { return TokenizerType::bert; }
        bool update_remote_embedder_apikey(const std::string& /*api_key*/) { return false; }
        const bool is_image_embedding() const { return false; }

    private:
        std::string vocab_file_name;
        size_t num_dim = 0;
};
