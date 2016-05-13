#pragma once

#include "system_input.hpp"

#include "bkassert/assert.hpp"

#include <functional>
#include <vector>
#include <deque>
#include <utility>

#include <cstdint>

namespace boken { enum class command_type : uint32_t; }

namespace boken {

enum class event_result : uint32_t {
    filter              //!< filter the event
  , filter_detach       //!< detach and filter the event
  , pass_through        //!< pass through to the next handler
  , pass_through_detach //!< detach and pass through to the next handler
};

//! A context used to process events generated by the system.
class input_context {
public:
    input_context() = default;
    input_context(char const* const name)
      : debug_name {name}
    {
    }

    event_result on_key(kb_event const event, kb_modifiers const kmods) {
        return on_key_handler
          ? on_key_handler(event, kmods)
          : event_result::pass_through;
    }

    event_result on_text_input(text_input_event const event) {
        return on_text_input_handler
          ? on_text_input_handler(event)
          : event_result::pass_through;
    }

    event_result on_mouse_button(mouse_event const event, kb_modifiers const kmods) {
        return on_mouse_button_handler
          ? on_mouse_button_handler(event, kmods)
          : event_result::pass_through;
    }

    event_result on_mouse_move(mouse_event const event, kb_modifiers const kmods) {
        return on_mouse_move_handler
          ? on_mouse_move_handler(event, kmods)
          : event_result::pass_through;
    }

    event_result on_mouse_wheel(int const wy, int const wx, kb_modifiers const kmods) {
        return on_mouse_wheel_handler
          ? on_mouse_wheel_handler(wy, wx, kmods)
          : event_result::pass_through;
    }

    event_result on_command(command_type const type, uintptr_t const data) {
        return on_command_handler
          ? on_command_handler(type, data)
          : event_result::pass_through;
    }

    std::function<event_result (kb_event, kb_modifiers)>    on_key_handler;
    std::function<event_result (text_input_event)>          on_text_input_handler;
    std::function<event_result (mouse_event, kb_modifiers)> on_mouse_button_handler;
    std::function<event_result (mouse_event, kb_modifiers)> on_mouse_move_handler;
    std::function<event_result (int, int, kb_modifiers)>    on_mouse_wheel_handler;
    std::function<event_result (command_type, uintptr_t)>   on_command_handler;

    char const* debug_name = "{anonymous}";
};

//! The stack of active input contexts. Events are first processed by the top
//! most context and continue down the stack unless filtered by an intervening
//! context.
class input_context_stack {
public:
    using id_t = uint32_t;

    size_t size() const noexcept {
        return contexts_.size();
    }

    id_t push(input_context context) {
        auto const id = get_next_id_();
        contexts_.push_back({std::move(context), id});
        return id;
    }

    void pop(id_t const id) {
        BK_ASSERT(!contexts_.empty());

        auto const first = contexts_.rbegin();
        auto const last  = contexts_.rend();

        auto const it = std::find_if(first, last
          , [id](pair_t const& p) noexcept {
                return id == p.second;
            });

        BK_ASSERT((it != last) && (it->second == id));

        if (id == next_id_ - 1) {
            --next_id_;
        } else {
            free_ids_.push_back(id);
        }

        contexts_.erase(std::next(it).base());
    }

    //! @returns true if the event has not been filtered, false otherwise.
    template <typename... Args0, typename... Args1>
    bool process(
        event_result (input_context::* const handler)(Args0...)
      , Args1&&... args
    ) {
        // as a stack: back to front
        for (auto i = size(); i > 0; --i) {
            auto const where   = i - 1u; // size == 1 ~> index == 0
            auto&      context = contexts_[where].first;
            auto const id      = contexts_[where].second;

            auto const result =
                (context.*handler)(std::forward<Args1>(args)...);

            switch (result) {
            case event_result::filter_detach :
                pop(id);
                BK_ATTRIBUTE_FALLTHROUGH;
            case event_result::filter :
                return false;
            case event_result::pass_through_detach :
                pop(id);
                BK_ATTRIBUTE_FALLTHROUGH;
            case event_result::pass_through :
                break;
            default:
                BK_ASSERT(false);
                break;
            }
        }

        return true;
    }
private:
    using pair_t = std::pair<input_context, id_t>;

    id_t get_next_id_() {
        if (!free_ids_.empty()) {
            auto const result = free_ids_.front();
            free_ids_.pop_front();
            return result;
        }

        return ++next_id_;
    }

    std::deque<id_t>    free_ids_;
    std::vector<pair_t> contexts_;
    id_t                next_id_ {};
};

} // namespace boken