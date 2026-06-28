#pragma once

// Mock of text_embedder_tokenizer.h for builds without AI features.
// Provides the enum, POD types and abstract base that the rest of the engine
// references, without the sentencepiece / onnxruntime-extensions tokenizers.

#include <vector>
#include <string>
#include <cstdint>

enum class TokenizerType {
    bert,
    distilbert,
    xlm_roberta,
    clip
};

struct encoded_input_t {
    std::vector<int64_t> input_ids;
    std::vector<int64_t> token_type_ids;
    std::vector<int64_t> attention_mask;
};

struct batch_encoded_input_t {
    std::vector<std::vector<int64_t>> input_ids;
    std::vector<std::vector<int64_t>> token_type_ids;
    std::vector<std::vector<int64_t>> attention_mask;
};

class TextEmbeddingTokenizer {
    public:
        virtual encoded_input_t Encode(const std::string& text) = 0;
        virtual ~TextEmbeddingTokenizer() = default;
        virtual TokenizerType get_tokenizer_type() = 0;
};
