#include "bth/core/runtime.hpp"

#include <cstring>

namespace bth {

const char* status_name(Status status) noexcept {
    switch (status) {
        case Status::ok: return "ok";
        case Status::invalid_argument: return "invalid_argument";
        case Status::invalid_state: return "invalid_state";
        case Status::capacity_exceeded: return "capacity_exceeded";
        case Status::duplicate_module: return "duplicate_module";
        case Status::missing_dependency: return "missing_dependency";
        case Status::dependency_order: return "dependency_order";
        case Status::hardware_error: return "hardware_error";
        case Status::no_memory: return "no_memory";
        case Status::internal_error: return "internal_error";
    }
    return "unknown";
}

const char* phase_name(Phase phase) noexcept {
    switch (phase) {
        case Phase::none: return "none";
        case Phase::registration: return "registration";
        case Phase::validation: return "validation";
        case Phase::init: return "init";
        case Phase::start: return "start";
        case Phase::tick: return "tick";
    }
    return "unknown";
}

Status Runtime::fail(Status status, Phase phase, const char* module,
                     const char* dependency) noexcept {
    failure_ = {status, phase, module, dependency};
    return status;
}

Status Runtime::add(Module& module) noexcept {
    if (state_ != RuntimeState::configuring) {
        return fail(Status::invalid_state, Phase::registration, module.id());
    }
    const char* id = module.id();
    if (id == nullptr || id[0] == '\0') {
        return fail(Status::invalid_argument, Phase::registration, id);
    }
    for (std::size_t i = 0; i < count_; ++i) {
        if (std::strcmp(modules_[i]->id(), id) == 0) {
            return fail(Status::duplicate_module, Phase::registration, id);
        }
    }
    if (count_ == max_modules) {
        return fail(Status::capacity_exceeded, Phase::registration, id);
    }
    modules_[count_++] = &module;
    failure_ = {};
    return Status::ok;
}

Status Runtime::validate() noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        const auto dependencies = modules_[i]->dependencies();
        if (dependencies.count != 0 && dependencies.ids == nullptr) {
            return fail(Status::invalid_argument, Phase::validation, modules_[i]->id());
        }
        for (std::size_t d = 0; d < dependencies.count; ++d) {
            const char* id = dependencies.ids[d];
            if (id == nullptr || id[0] == '\0') {
                return fail(Status::invalid_argument, Phase::validation, modules_[i]->id());
            }
            std::size_t found = 0;
            while (found < count_ && std::strcmp(modules_[found]->id(), id) != 0) {
                ++found;
            }
            if (found == count_) {
                return fail(Status::missing_dependency, Phase::validation, modules_[i]->id(), id);
            }
            if (found >= i) {
                return fail(Status::dependency_order, Phase::validation, modules_[i]->id(), id);
            }
        }
    }
    return Status::ok;
}

Status Runtime::start() noexcept {
    if (state_ != RuntimeState::configuring && state_ != RuntimeState::stopped) {
        return fail(Status::invalid_state, Phase::start, nullptr);
    }
    failure_ = {};
    const auto validation = validate();
    if (validation != Status::ok) {
        state_ = RuntimeState::failed;
        return validation;
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const auto result = modules_[i]->init();
        // Include the attempted module in rollback, even on partial initialization.
        ++initialized_;
        if (result != Status::ok) {
            fail(result, Phase::init, modules_[i]->id());
            rollback();
            state_ = RuntimeState::failed;
            return result;
        }
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const auto result = modules_[i]->start();
        ++started_;
        if (result != Status::ok) {
            fail(result, Phase::start, modules_[i]->id());
            rollback();
            state_ = RuntimeState::failed;
            return result;
        }
    }
    state_ = RuntimeState::running;
    return Status::ok;
}

Status Runtime::tick(std::uint64_t now_ms) noexcept {
    if (state_ != RuntimeState::running) {
        return fail(Status::invalid_state, Phase::tick, nullptr);
    }
    for (std::size_t i = 0; i < count_; ++i) {
        const auto result = modules_[i]->tick(now_ms);
        if (result != Status::ok) {
            fail(result, Phase::tick, modules_[i]->id());
            rollback();
            state_ = RuntimeState::failed;
            return result;
        }
    }
    return Status::ok;
}

void Runtime::rollback() noexcept {
    while (started_ != 0) {
        modules_[--started_]->stop();
    }
    while (initialized_ != 0) {
        modules_[--initialized_]->deinit();
    }
}

void Runtime::stop() noexcept {
    rollback();
    state_ = RuntimeState::stopped;
    // Keep the last failure available for diagnostics until the next start.
}

}  // namespace bth
