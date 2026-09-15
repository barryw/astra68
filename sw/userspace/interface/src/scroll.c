#include <astra/scroll.h>

#include <astra/bytes.h>

#include <limits.h>

static uint32_t maximum(uint32_t content, uint32_t viewport)
{
    return content > viewport ? content - viewport : 0u;
}

static int marks_valid(const AstraScrollMark *marks, uint32_t count)
{
    if (count != 0u && marks == NULL)
        return 0;
    for (uint32_t at = 0u; at < count; ++at)
        if (marks[at].kind < ASTRA_SCROLL_MARK_INFORMATION ||
            marks[at].kind > ASTRA_SCROLL_MARK_FAULT ||
            marks[at].reserved != 0u)
            return 0;
    return 1;
}

static int model_valid(const AstraScrollModel *model)
{
    return model != NULL &&
           model->_private_structure_size == sizeof(*model) &&
           model->_private_version == ASTRA_SCROLL_MODEL_VERSION &&
           model->_private_generation != 0u &&
           model->_private_line_width != 0u &&
           model->_private_line_height != 0u &&
           model->_private_offset_x <= maximum(
               model->_private_content_width,
               model->_private_viewport_width) &&
           model->_private_offset_y <= maximum(
               model->_private_content_height,
               model->_private_viewport_height) &&
           marks_valid(model->_private_marks, model->_private_mark_count) &&
           astra_words_zero(model->_private_reserved, 3u);
}

static void change(AstraScrollModel *model)
{
    if (++model->_private_generation == 0u)
        model->_private_generation = 1u;
}

AstraResult astra_scroll_init(AstraScrollModel *model,
                              const AstraScrollModelInfo *info)
{
    if (model == NULL || info == NULL || info->size < sizeof(*info) ||
        info->line_width == 0u || info->line_height == 0u ||
        info->offset_x > maximum(info->content_width,
                                 info->viewport_width) ||
        info->offset_y > maximum(info->content_height,
                                 info->viewport_height) ||
        !marks_valid(info->marks, info->mark_count) ||
        !astra_words_zero(info->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *model = (AstraScrollModel)ASTRA_SCROLL_MODEL_INIT;
    model->_private_generation = 1u;
    model->_private_content_width = info->content_width;
    model->_private_content_height = info->content_height;
    model->_private_viewport_width = info->viewport_width;
    model->_private_viewport_height = info->viewport_height;
    model->_private_offset_x = info->offset_x;
    model->_private_offset_y = info->offset_y;
    model->_private_line_width = info->line_width;
    model->_private_line_height = info->line_height;
    model->_private_marks = info->marks;
    model->_private_mark_count = info->mark_count;
    return ASTRA_OK;
}

AstraResult astra_scroll_get_state(const AstraScrollModel *model,
                                   AstraScrollState *state)
{
    if (!model_valid(model) || state == NULL || state->size < sizeof(*state))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *state = (AstraScrollState)ASTRA_SCROLL_STATE_INIT;
    state->generation = model->_private_generation;
    state->content_width = model->_private_content_width;
    state->content_height = model->_private_content_height;
    state->viewport_width = model->_private_viewport_width;
    state->viewport_height = model->_private_viewport_height;
    state->offset_x = model->_private_offset_x;
    state->offset_y = model->_private_offset_y;
    state->maximum_x = maximum(state->content_width, state->viewport_width);
    state->maximum_y = maximum(state->content_height, state->viewport_height);
    state->line_width = model->_private_line_width;
    state->line_height = model->_private_line_height;
    state->marks = model->_private_marks;
    state->mark_count = model->_private_mark_count;
    return ASTRA_OK;
}

AstraResult astra_scroll_set_extents(AstraScrollModel *model,
                                     uint32_t content_width,
                                     uint32_t content_height,
                                     uint32_t viewport_width,
                                     uint32_t viewport_height)
{
    uint32_t offset_x;
    uint32_t offset_y;

    if (!model_valid(model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    offset_x = model->_private_offset_x;
    offset_y = model->_private_offset_y;
    if (offset_x > maximum(content_width, viewport_width))
        offset_x = maximum(content_width, viewport_width);
    if (offset_y > maximum(content_height, viewport_height))
        offset_y = maximum(content_height, viewport_height);
    if (model->_private_content_width == content_width &&
        model->_private_content_height == content_height &&
        model->_private_viewport_width == viewport_width &&
        model->_private_viewport_height == viewport_height &&
        model->_private_offset_x == offset_x &&
        model->_private_offset_y == offset_y)
        return ASTRA_OK;
    model->_private_content_width = content_width;
    model->_private_content_height = content_height;
    model->_private_viewport_width = viewport_width;
    model->_private_viewport_height = viewport_height;
    model->_private_offset_x = offset_x;
    model->_private_offset_y = offset_y;
    change(model);
    return ASTRA_OK;
}

AstraResult astra_scroll_set_offset(AstraScrollModel *model,
                                    uint32_t offset_x, uint32_t offset_y)
{
    if (!model_valid(model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (offset_x > maximum(model->_private_content_width,
                           model->_private_viewport_width))
        offset_x = maximum(model->_private_content_width,
                           model->_private_viewport_width);
    if (offset_y > maximum(model->_private_content_height,
                           model->_private_viewport_height))
        offset_y = maximum(model->_private_content_height,
                           model->_private_viewport_height);
    if (model->_private_offset_x == offset_x &&
        model->_private_offset_y == offset_y)
        return ASTRA_OK;
    model->_private_offset_x = offset_x;
    model->_private_offset_y = offset_y;
    change(model);
    return ASTRA_OK;
}

AstraResult astra_scroll_by(AstraScrollModel *model,
                            int32_t delta_x, int32_t delta_y)
{
    int64_t x;
    int64_t y;

    if (!model_valid(model))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    x = (int64_t)model->_private_offset_x + delta_x;
    y = (int64_t)model->_private_offset_y + delta_y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if ((uint64_t)x > maximum(model->_private_content_width,
                              model->_private_viewport_width))
        x = maximum(model->_private_content_width,
                    model->_private_viewport_width);
    if ((uint64_t)y > maximum(model->_private_content_height,
                              model->_private_viewport_height))
        y = maximum(model->_private_content_height,
                    model->_private_viewport_height);
    return astra_scroll_set_offset(model, (uint32_t)x, (uint32_t)y);
}

AstraResult astra_scroll_set_marks(AstraScrollModel *model,
                                   const AstraScrollMark *marks,
                                   uint32_t mark_count)
{
    if (!model_valid(model) || !marks_valid(marks, mark_count))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (model->_private_marks == marks &&
        model->_private_mark_count == mark_count)
        return ASTRA_OK;
    model->_private_marks = marks;
    model->_private_mark_count = mark_count;
    change(model);
    return ASTRA_OK;
}
