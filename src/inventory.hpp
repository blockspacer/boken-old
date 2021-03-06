#pragma once

#include "types.hpp"
#include "config.hpp"
#include "context.hpp"
#include "math_types.hpp"

#include <string>
#include <functional>
#include <initializer_list>
#include <memory>
#include <cstdint>

namespace boken { class text_renderer; }
namespace boken { class text_layout; }
namespace boken { class item; }

namespace boken {
class inventory_list {
public:
    //! function used to get the text for a cell from an item instance.
    using get_f = std::function<std::string (const_item_descriptor)>;

    //! function that has the semantics of std::sting::compare.
    using sort_f = std::function<int (const_item_descriptor, string_view
                                    , const_item_descriptor, string_view)>;

    //! insert new column of row and the end
    static int const insert_at_end = -1;

    //! use a dynamically adjustable width for the column in lieu of static.
    static int16_t const adjust_to_fit = -1;

    struct layout_metrics {
        recti32 frame;
        recti32 client_frame;
        recti32 title;
        recti32 close_button;
        recti32 scroll_bar_v;
        recti32 scroll_bar_h;

        sizei32y header_h;
    };

    struct hit_test_result {
        enum class type : uint32_t {
            none         //!< no hit
          , empty        //!< an empty area of the list
          , header       //!< column header
          , cell         //!< table cell
          , title        //!< window title
          , frame        //!< window frame
          , button_close //!< window close button
          , scroll_bar_v //!< the vertical scroll bar
          , scroll_bar_h //!< the horizontal scroll bar
        };

        explicit operator bool() const noexcept { return what != type::none; }

        type    what;
        int32_t x;
        int32_t y;
    };

    struct column_info {
        text_layout const& text;
        sizei16x min_width;
        sizei16x max_width;
        sizei16x width;
        uint8_t  id;
    };

    //--------------------------------------------------------------------------
    virtual ~inventory_list();

    //--------------------------------------------------------------------------
    virtual void set_title(std::string title) = 0;

    //--------------------------------------------------------------------------
    virtual text_layout const& title() const noexcept = 0;
    virtual string_view title_text() const noexcept = 0;
    virtual layout_metrics metrics() const noexcept = 0;
    virtual recti32 cell_bounds(int col, int row) const noexcept = 0;
    virtual vec2i32 scroll_offset() const noexcept = 0;

    //--------------------------------------------------------------------------
    virtual bool show() noexcept = 0;
    virtual bool hide() noexcept = 0;
    virtual bool is_visible() const noexcept = 0;
    virtual bool toggle_visible() noexcept = 0;

    //--------------------------------------------------------------------------
    virtual size_t size()  const noexcept = 0;
    virtual bool   empty() const noexcept = 0;

    virtual size_t rows() const noexcept = 0;
    virtual size_t cols() const noexcept = 0;

    //--------------------------------------------------------------------------
    virtual void scroll_by(sizei32y dy) noexcept = 0;
    virtual void scroll_by(sizei32x dx) noexcept = 0;

    virtual void scroll_into_view(int c, int r) noexcept = 0;

    //--------------------------------------------------------------------------
    virtual void resize_to(sizei32x w, sizei32y h) noexcept = 0;
    virtual void resize_by(sizei32x dw, sizei32y dh, int side_x, int side_y) noexcept = 0;

    virtual void move_to(point2i32 p) noexcept = 0;
    virtual void move_by(vec2i32 v) noexcept = 0;

    //--------------------------------------------------------------------------
    virtual hit_test_result hit_test(point2i32 p) const noexcept = 0;

    //--------------------------------------------------------------------------
    virtual int  indicated() const noexcept = 0;

    //! sets the indicator to @p n and returns the index of the previously
    //! indicated item.
    virtual int indicate(int n) noexcept = 0;

    //! sets the indicator to the next item and returns the index of the
    //! previously indicated item.
    virtual int indicate_next(int n = 1) noexcept = 0;

    //! sets the indicator to the previous item and returns the index of the
    //! previously indicated item.
    virtual int indicate_prev(int n = 1) noexcept = 0;

    //--------------------------------------------------------------------------

    //! @param cols A set of successive columns to sort by: first by cols[0],
    //!             then by cols[1] if cols[0] compare equal, and so on.
    //!             If cols[i] is negative, sort in descending order, ascending
    //!             oder otherwise.
    //! @note column indicies are 1 based
    virtual void sort(std::initializer_list<int> cols) noexcept = 0;

    virtual void sort(int const* first, int const* last) noexcept = 0;

    //--------------------------------------------------------------------------
    virtual void reserve(size_t cols, size_t rows) = 0;

    //! @note The functor get_f is copied internally; any state must be captured
    //!       by value: be careful of dangling references.
    virtual void add_column(
        uint8_t     id
      , std::string label
      , get_f       get
      , sort_f      sort
      , int         insert_before = insert_at_end
      , sizei16x    width         = adjust_to_fit) = 0;

    virtual void add_row(item_instance_id id) = 0;
    virtual void add_rows(item_instance_id const* first, item_instance_id const* last) = 0;

    virtual void remove_row(int i) noexcept = 0;
    virtual void remove_rows(int const* first, int const* last) noexcept = 0;

    virtual void clear_rows() noexcept = 0;
    virtual void clear() noexcept = 0;

    //--------------------------------------------------------------------------
    virtual bool selection_toggle(int row) = 0;
    virtual void selection_set(std::initializer_list<int> rows) = 0;
    virtual void selection_union(std::initializer_list<int> rows) = 0;
    virtual int  selection_clear() = 0;

    //! Get an iterator range indicating the indicies of the currently selected
    //! items.
    //! @returns {nullptr, nullptr} to indicate the absence of any selection.
    //! @returns {first, last + 1} when a selection exists.
    virtual std::pair<int const*, int const*> get_selection() const = 0;

    virtual bool is_selected(int row) const noexcept = 0;

    //--------------------------------------------------------------------------
    virtual column_info col(int index) const noexcept = 0;

    //--------------------------------------------------------------------------
    virtual std::pair<text_layout const*, text_layout const*>
        row(int index) const noexcept = 0;

    virtual item_instance_id row_data(int index) const noexcept = 0;

    //--------------------------------------------------------------------------
    virtual void layout() noexcept = 0;
};

std::unique_ptr<inventory_list> make_inventory_list(const_context  ctx
                                                  , text_renderer& trender);

} //namespace boken
