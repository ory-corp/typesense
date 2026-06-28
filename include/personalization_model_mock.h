#pragma once

// Mock of personalization_model.h for builds without AI features.
// Same public interface as the real PersonalizationModel, but with no
// onnxruntime dependency. Validation/creation fail cleanly so the model
// management endpoints return a clear error.

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <json.hpp>
#include "option.h"
#include "embedder_manager.h"          // matches the real header's transitive includes
#include "text_embedder_tokenizer.h"   // batch_encoded_input_t (resolves to the mock)
#include "text_embedder_remote.h"      // embedding_res_t
#include "ai_mock_common.h"

class PersonalizationModel {
public:
    static inline const std::map<std::string, std::vector<std::string>> valid_model_names = {
        {"recommendation", {"tyrec-1"}},
        {"search", {"tyrec-2"}}
    };

    PersonalizationModel(const std::string& model_id) : model_id_(model_id) {}
    ~PersonalizationModel() {}

    static std::string get_model_subdir(const std::string& model_id) { return "per_" + model_id; }
    static Option<bool> validate_model(const nlohmann::json& /*model_json*/) {
        return Option<bool>(400, DISABLED_MSG);
    }
    static Option<nlohmann::json> create_model(const std::string& /*model_id*/, const nlohmann::json& /*model_json*/,
                                               const std::string /*model_data*/) {
        return Option<nlohmann::json>(400, DISABLED_MSG);
    }
    static Option<nlohmann::json> update_model(const std::string& /*model_id*/, const nlohmann::json& /*model_json*/,
                                               const std::string /*model_data*/) {
        return Option<nlohmann::json>(400, DISABLED_MSG);
    }
    static Option<bool> delete_model(const std::string& /*model_id*/) { return Option<bool>(true); }

    size_t get_num_dim() const { return num_dim_; }
    embedding_res_t embed_recommendations(const std::vector<std::vector<float>>& /*input_vector*/,
                                          const std::vector<int64_t>& /*user_mask*/) {
        return ai_disabled::embedding_res(DISABLED_MSG);
    }
    embedding_res_t embed_user(const std::vector<std::string>& /*features*/) {
        return ai_disabled::embedding_res(DISABLED_MSG);
    }
    embedding_res_t embed_item(const std::vector<std::string>& /*features*/) {
        return ai_disabled::embedding_res(DISABLED_MSG);
    }
    std::vector<embedding_res_t> batch_embed_recommendations(const std::vector<std::vector<std::vector<float>>>& input_vectors,
                                                             const std::vector<std::vector<int64_t>>& /*user_masks*/) {
        return std::vector<embedding_res_t>(input_vectors.size(), ai_disabled::embedding_res(DISABLED_MSG));
    }
    std::vector<embedding_res_t> batch_embed_users(const std::vector<std::vector<std::string>>& features) {
        return std::vector<embedding_res_t>(features.size(), ai_disabled::embedding_res(DISABLED_MSG));
    }
    std::vector<embedding_res_t> batch_embed_items(const std::vector<std::vector<std::string>>& features) {
        return std::vector<embedding_res_t>(features.size(), ai_disabled::embedding_res(DISABLED_MSG));
    }
    batch_encoded_input_t encode_features(const std::vector<std::string>& /*features*/) { return batch_encoded_input_t{}; }
    std::vector<batch_encoded_input_t> encode_batch(const std::vector<std::vector<std::string>>& /*batch_features*/) { return {}; }
    Option<bool> validate_model_io() { return Option<bool>(400, DISABLED_MSG); }

private:
    static constexpr const char* DISABLED_MSG =
        "Personalization models are disabled in this build (built without AI features).";
    std::string model_id_;
    size_t num_dim_ = 0;
};
