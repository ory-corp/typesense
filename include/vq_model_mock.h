#pragma once

// Mock of vq_model.h for builds without AI features (no whisper.cpp).
// The VQModel base is identical to the real one (it is whisper-free); only the
// concrete WhisperModel is replaced with a non-functional stub.

#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>

#include "string_utils.h"
#include "option.h"

class VQModel {
    protected:
        int collection_ref_count = 0;
        std::shared_mutex collection_ref_count_mutex;
        std::string model_name;
    public:
        virtual ~VQModel() = default;
        virtual Option<std::string> transcribe(const std::string& audio) = 0;
        void inc_collection_ref_count() {
            std::unique_lock<std::shared_mutex> lock(collection_ref_count_mutex);
            collection_ref_count++;
        }
        void dec_collection_ref_count() {
            std::unique_lock<std::shared_mutex> lock(collection_ref_count_mutex);
            collection_ref_count--;
        }
        int get_collection_ref_count() {
            std::shared_lock<std::shared_mutex> lock(collection_ref_count_mutex);
            return collection_ref_count;
        }
        const std::string& get_model_name() {
            return model_name;
        }
        VQModel(const std::string& model_name) : model_name(model_name) {}
};

// Forward declaration only: keeps WhisperModel's signatures intact for the
// (real, still-compiled) vq model manager without pulling in whisper.cpp.
struct whisper_context;

class WhisperModel : public VQModel {
    public:
        WhisperModel() = delete;
        WhisperModel(whisper_context* /*ctx*/, const std::string& model_name) : VQModel(model_name) {}
        static whisper_context* validate_and_load_model(const std::string& /*model_path*/) { return nullptr; }
        ~WhisperModel() {}
        Option<std::string> transcribe(const std::string& /*audio_base64*/) override {
            return Option<std::string>(400, "Voice query (audio) models are disabled in this build (built without AI features).");
        }
};
