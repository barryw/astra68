#include <astra/text_model.h>

#include <astra/bytes.h>
#include <astra/utf8.h>

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct TextPiece {
    uint32_t offset;
    uint32_t length;
} TextPiece;

typedef struct EditPlan {
    TextPiece pieces[5];
    uint32_t piece_count;
    uint32_t replace_first;
    uint32_t replace_after;
    uint32_t result_piece_count;
    uint32_t result_line_count;
    uint32_t result_text_bytes;
    uint32_t content_required;
    uint32_t metadata_required;
    uint32_t inserted_newlines;
} EditPlan;

static uint32_t next_generation(uint32_t generation)
{
    ++generation;
    return generation == 0u ? 1u : generation;
}

static int ranges_overlap(const void *left, uint32_t left_bytes,
                          const void *right, uint32_t right_bytes)
{
    uintptr_t left_start;
    uintptr_t left_end;
    uintptr_t right_start;
    uintptr_t right_end;

    if (left_bytes == 0u || right_bytes == 0u)
        return 0;
    if (left == NULL || right == NULL)
        return 1;
    left_start = (uintptr_t)left;
    right_start = (uintptr_t)right;
    if (left_start > UINTPTR_MAX - left_bytes ||
        right_start > UINTPTR_MAX - right_bytes)
        return 1;
    left_end = left_start + left_bytes;
    right_end = right_start + right_bytes;
    return left_start < right_end && right_start < left_end;
}

static uint32_t metadata_limit(const AstraTextModel *model)
{
    return model->_private_metadata_capacity & ~UINT32_C(3);
}

static TextPiece *pieces(AstraTextModel *model)
{
    return (TextPiece *)(void *)model->_private_metadata;
}

static const TextPiece *const_pieces(const AstraTextModel *model)
{
    return (const TextPiece *)(const void *)model->_private_metadata;
}

static uint32_t line_offset(const AstraTextModel *model, uint32_t index)
{
    const uint32_t *slot = (const uint32_t *)(const void *)(
        model->_private_metadata + metadata_limit(model) -
        (index + 1u) * sizeof(uint32_t));

    return *slot;
}

static void line_offset_set(AstraTextModel *model, uint32_t index,
                            uint32_t value)
{
    uint32_t *slot = (uint32_t *)(void *)(
        model->_private_metadata + metadata_limit(model) -
        (index + 1u) * sizeof(uint32_t));

    *slot = value;
}

static int empty_model(const AstraTextModel *model)
{
    return model != NULL &&
           model->_private_structure_size == sizeof(*model) &&
           model->_private_content == NULL &&
           model->_private_content_capacity == 0u &&
           model->_private_content_used == 0u &&
           model->_private_metadata == NULL &&
           model->_private_metadata_capacity == 0u &&
           model->_private_piece_count == 0u &&
           model->_private_line_count == 0u &&
           model->_private_text_bytes == 0u &&
           model->_private_anchor == 0u && model->_private_focus == 0u &&
           model->_private_generation == 0u &&
           astra_words_zero(model->_private_reserved, 4u);
}

static int model_valid(const AstraTextModel *model)
{
    uint64_t occupied;

    if (model == NULL ||
        model->_private_structure_size != sizeof(*model) ||
        (model->_private_content_capacity != 0u &&
         model->_private_content == NULL) ||
        model->_private_content_used > model->_private_content_capacity ||
        model->_private_metadata == NULL ||
        ((uintptr_t)model->_private_metadata & 3u) != 0u ||
        model->_private_line_count == 0u ||
        model->_private_anchor > model->_private_text_bytes ||
        model->_private_focus > model->_private_text_bytes ||
        model->_private_generation == 0u ||
        (model->_private_text_bytes == 0u ?
             model->_private_piece_count != 0u :
             model->_private_piece_count == 0u) ||
        !astra_words_zero(model->_private_reserved, 4u))
        return 0;
    occupied = (uint64_t)model->_private_piece_count * sizeof(TextPiece) +
               (uint64_t)model->_private_line_count * sizeof(uint32_t);
    return occupied <= metadata_limit(model) &&
           !ranges_overlap(model->_private_content,
                           model->_private_content_capacity,
                           model->_private_metadata,
                           model->_private_metadata_capacity);
}

static int piece_valid(const AstraTextModel *model, const TextPiece *piece)
{
    return piece->length != 0u &&
           piece->offset <= model->_private_content_used &&
           piece->length <= model->_private_content_used - piece->offset;
}

static int locate(const AstraTextModel *model, uint32_t position,
                  uint32_t *piece_index, uint32_t *inside)
{
    const TextPiece *table = const_pieces(model);
    uint32_t logical = 0u;

    if (position > model->_private_text_bytes)
        return 0;
    for (uint32_t index = 0u; index < model->_private_piece_count; ++index) {
        const TextPiece *piece = &table[index];

        if (!piece_valid(model, piece) ||
            logical > model->_private_text_bytes - piece->length)
            return 0;
        if (position < logical + piece->length) {
            *piece_index = index;
            *inside = position - logical;
            return 1;
        }
        logical += piece->length;
    }
    if (logical != model->_private_text_bytes || position != logical)
        return 0;
    *piece_index = model->_private_piece_count;
    *inside = 0u;
    return 1;
}

static int document_byte(const AstraTextModel *model, uint32_t position,
                         uint8_t *byte)
{
    uint32_t index;
    uint32_t inside;

    if (position >= model->_private_text_bytes ||
        !locate(model, position, &index, &inside) ||
        index >= model->_private_piece_count)
        return 0;
    *byte = model->_private_content[
        const_pieces(model)[index].offset + inside];
    return 1;
}

static int boundary(const AstraTextModel *model, uint32_t position)
{
    uint8_t byte;

    if (position == model->_private_text_bytes)
        return 1;
    return document_byte(model, position, &byte) && (byte & 0xc0u) != 0x80u;
}

static uint32_t count_newlines(const char *text, uint32_t bytes)
{
    uint32_t count = 0u;

    for (uint32_t at = 0u; at < bytes; ++at)
        if (text[at] == '\n')
            ++count;
    return count;
}

static int count_document_newlines(const AstraTextModel *model,
                                   uint32_t start, uint32_t end,
                                   uint32_t *count)
{
    const TextPiece *table = const_pieces(model);
    uint32_t logical = 0u;
    uint32_t total = 0u;

    for (uint32_t index = 0u;
         index < model->_private_piece_count && logical < end; ++index) {
        const TextPiece *piece = &table[index];
        uint32_t piece_end;
        uint32_t first;
        uint32_t last;

        if (!piece_valid(model, piece) ||
            logical > model->_private_text_bytes - piece->length)
            return 0;
        piece_end = logical + piece->length;
        first = start > logical ? start - logical : 0u;
        last = end < piece_end ? end - logical : piece->length;
        for (uint32_t at = first; at < last; ++at)
            if (model->_private_content[piece->offset + at] == '\n')
                ++total;
        logical = piece_end;
    }
    *count = total;
    return 1;
}

static void append_piece(EditPlan *plan, TextPiece piece)
{
    TextPiece *last;

    if (piece.length == 0u)
        return;
    if (plan->piece_count != 0u) {
        last = &plan->pieces[plan->piece_count - 1u];
        if ((uint64_t)last->offset + last->length == piece.offset) {
            last->length += piece.length;
            return;
        }
    }
    plan->pieces[plan->piece_count++] = piece;
}

static AstraResult make_plan(const AstraTextModel *model,
                             uint32_t start, uint32_t end,
                             const char *replacement,
                             uint32_t replacement_bytes, EditPlan *plan)
{
    const TextPiece *table;
    uint32_t start_index;
    uint32_t start_inside;
    uint32_t end_index;
    uint32_t end_inside;
    uint32_t affected_first;
    uint32_t affected_after;
    uint32_t removed_newlines;
    uint64_t result_length;
    uint64_t content_required;
    uint64_t metadata_required;

    if (!model_valid(model) || plan == NULL || start > end ||
        end > model->_private_text_bytes ||
        (replacement == NULL && replacement_bytes != 0u) ||
        !astra_utf8_validate(replacement, replacement_bytes,
                             ASTRA_UTF8_ALLOW_NUL) ||
        !boundary(model, start) || !boundary(model, end) ||
        !locate(model, start, &start_index, &start_inside) ||
        !locate(model, end, &end_index, &end_inside))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result_length = (uint64_t)model->_private_text_bytes - (end - start) +
                    replacement_bytes;
    content_required = (uint64_t)model->_private_content_used +
                       replacement_bytes;
    if (result_length > UINT32_MAX || content_required > UINT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    if (!count_document_newlines(model, start, end, &removed_newlines))
        return ASTRA_ERROR_INVALID_ARGUMENT;

    *plan = (EditPlan){0};
    plan->inserted_newlines = count_newlines(replacement, replacement_bytes);
    if (plan->inserted_newlines > UINT32_MAX -
                                      (model->_private_line_count -
                                       removed_newlines))
        return ASTRA_ERROR_NO_RESOURCES;
    plan->result_line_count = model->_private_line_count - removed_newlines +
                              plan->inserted_newlines;
    plan->result_text_bytes = (uint32_t)result_length;
    plan->content_required = (uint32_t)content_required;
    table = const_pieces(model);

    if (start == end && start_index < model->_private_piece_count &&
        start_inside != 0u) {
        affected_first = start_index;
        affected_after = start_index + 1u;
    } else {
        affected_first = start_index;
        affected_after = end_index + (end_inside != 0u ? 1u : 0u);
    }
    plan->replace_first = affected_first;
    plan->replace_after = affected_after;
    if (plan->replace_first != 0u) {
        --plan->replace_first;
        append_piece(plan, table[plan->replace_first]);
    }
    if (start_index < model->_private_piece_count && start_inside != 0u)
        append_piece(plan, (TextPiece){table[start_index].offset,
                                      start_inside});
    append_piece(plan, (TextPiece){model->_private_content_used,
                                   replacement_bytes});
    if (end_index < model->_private_piece_count && end_inside != 0u)
        append_piece(plan, (TextPiece){
            table[end_index].offset + end_inside,
            table[end_index].length - end_inside});
    if (plan->replace_after < model->_private_piece_count) {
        append_piece(plan, table[plan->replace_after]);
        ++plan->replace_after;
    }
    plan->result_piece_count = model->_private_piece_count -
                               (plan->replace_after - plan->replace_first) +
                               plan->piece_count;
    metadata_required = (uint64_t)plan->result_piece_count *
                            sizeof(TextPiece) +
                        (uint64_t)plan->result_line_count * sizeof(uint32_t);
    if (metadata_required > UINT32_MAX)
        return ASTRA_ERROR_NO_RESOURCES;
    plan->metadata_required = (uint32_t)metadata_required;
    return ASTRA_OK;
}

AstraResult astra_text_model_init(AstraTextModel *model,
                                  const AstraTextModelInfo *info)
{
    uint32_t lines;
    uint64_t metadata_required;

    if (!empty_model(model) || info == NULL || info->size < sizeof(*info) ||
        (info->text == NULL && info->text_bytes != 0u) ||
        (info->content_arena == NULL && info->content_arena_bytes != 0u) ||
        info->text_bytes > info->content_arena_bytes ||
        info->metadata_arena == NULL ||
        ((uintptr_t)info->metadata_arena & 3u) != 0u ||
        info->selection.size < sizeof(info->selection) ||
        info->selection.anchor > info->text_bytes ||
        info->selection.focus > info->text_bytes ||
        !astra_words_zero(info->selection.reserved, 4u) ||
        !astra_words_zero(info->reserved, 4u) ||
        !astra_utf8_validate(info->text, info->text_bytes,
                             ASTRA_UTF8_ALLOW_NUL) ||
        (info->selection.anchor < info->text_bytes &&
         (((const uint8_t *)info->text)[info->selection.anchor] & 0xc0u) ==
             0x80u) ||
        (info->selection.focus < info->text_bytes &&
         (((const uint8_t *)info->text)[info->selection.focus] & 0xc0u) ==
             0x80u) ||
        ranges_overlap(info->content_arena, info->content_arena_bytes,
                       info->metadata_arena, info->metadata_arena_bytes) ||
        ranges_overlap(info->text, info->text_bytes,
                       info->metadata_arena, info->metadata_arena_bytes))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    lines = count_newlines(info->text, info->text_bytes) + 1u;
    metadata_required = (info->text_bytes == 0u ? 0u : sizeof(TextPiece)) +
                        (uint64_t)lines * sizeof(uint32_t);
    if (metadata_required > (info->metadata_arena_bytes & ~UINT32_C(3)))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;

    *model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
    model->_private_content = info->content_arena;
    model->_private_content_capacity = info->content_arena_bytes;
    model->_private_content_used = info->text_bytes;
    model->_private_metadata = info->metadata_arena;
    model->_private_metadata_capacity = info->metadata_arena_bytes;
    model->_private_piece_count = info->text_bytes == 0u ? 0u : 1u;
    model->_private_line_count = lines;
    model->_private_text_bytes = info->text_bytes;
    model->_private_anchor = info->selection.anchor;
    model->_private_focus = info->selection.focus;
    model->_private_generation = 1u;
    if (info->text_bytes != 0u) {
        memmove(model->_private_content, info->text, info->text_bytes);
        pieces(model)[0] = (TextPiece){0u, info->text_bytes};
    }
    line_offset_set(model, 0u, 0u);
    lines = 1u;
    for (uint32_t at = 0u; at < info->text_bytes; ++at)
        if (model->_private_content[at] == '\n')
            line_offset_set(model, lines++, at + 1u);
    return ASTRA_OK;
}

AstraResult astra_text_model_validate(const AstraTextModel *model)
{
    const TextPiece *table;
    uint32_t logical = 0u;
    uint32_t line = 1u;

    if (!model_valid(model) || line_offset(model, 0u) != 0u ||
        !boundary(model, model->_private_anchor) ||
        !boundary(model, model->_private_focus))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    table = const_pieces(model);
    for (uint32_t index = 0u; index < model->_private_piece_count; ++index) {
        const TextPiece *piece = &table[index];

        if (!piece_valid(model, piece) ||
            !astra_utf8_validate(model->_private_content + piece->offset,
                                 piece->length, ASTRA_UTF8_ALLOW_NUL) ||
            logical > model->_private_text_bytes - piece->length ||
            (index != 0u &&
             (uint64_t)table[index - 1u].offset +
                     table[index - 1u].length == piece->offset))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        for (uint32_t at = 0u; at < piece->length; ++at) {
            if (model->_private_content[piece->offset + at] == '\n') {
                if (line >= model->_private_line_count ||
                    line_offset(model, line) != logical + at + 1u)
                    return ASTRA_ERROR_INVALID_ARGUMENT;
                ++line;
            }
        }
        logical += piece->length;
    }
    return logical == model->_private_text_bytes &&
           line == model->_private_line_count ?
        ASTRA_OK : ASTRA_ERROR_INVALID_ARGUMENT;
}

AstraResult astra_text_model_get_state(const AstraTextModel *model,
                                       AstraTextModelState *state)
{
    if (!model_valid(model) || state == NULL || state->size < sizeof(*state) ||
        !astra_words_zero(state->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *state = (AstraTextModelState)ASTRA_TEXT_MODEL_STATE_INIT;
    state->generation = model->_private_generation;
    state->text_bytes = model->_private_text_bytes;
    state->line_count = model->_private_line_count;
    state->piece_count = model->_private_piece_count;
    state->content_bytes_used = model->_private_content_used;
    state->content_arena_bytes = model->_private_content_capacity;
    state->metadata_bytes_used =
        model->_private_piece_count * (uint32_t)sizeof(TextPiece) +
        model->_private_line_count * (uint32_t)sizeof(uint32_t);
    state->metadata_arena_bytes = model->_private_metadata_capacity;
    state->selection.anchor = model->_private_anchor;
    state->selection.focus = model->_private_focus;
    return ASTRA_OK;
}

AstraResult astra_text_model_set_selection(
    AstraTextModel *model, const AstraTextSelection *selection)
{
    if (!model_valid(model) || selection == NULL ||
        selection->size < sizeof(*selection) ||
        !astra_words_zero(selection->reserved, 4u) ||
        selection->anchor > model->_private_text_bytes ||
        selection->focus > model->_private_text_bytes ||
        !boundary(model, selection->anchor) ||
        !boundary(model, selection->focus))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (model->_private_anchor != selection->anchor ||
        model->_private_focus != selection->focus) {
        model->_private_anchor = selection->anchor;
        model->_private_focus = selection->focus;
        model->_private_generation = next_generation(model->_private_generation);
    }
    return ASTRA_OK;
}

AstraResult astra_text_model_replace_requirements(
    const AstraTextModel *model, uint32_t start, uint32_t end,
    const char *replacement, uint32_t replacement_bytes,
    AstraTextModelRequirements *requirements)
{
    EditPlan plan;
    AstraResult result;

    if (requirements == NULL || requirements->size < sizeof(*requirements) ||
        !astra_words_zero(requirements->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = make_plan(model, start, end, replacement, replacement_bytes,
                       &plan);
    if (result != ASTRA_OK)
        return result;
    *requirements =
        (AstraTextModelRequirements)ASTRA_TEXT_MODEL_REQUIREMENTS_INIT;
    requirements->content_arena_bytes = plan.content_required;
    requirements->metadata_arena_bytes = plan.metadata_required;
    requirements->piece_count = plan.result_piece_count;
    requirements->line_count = plan.result_line_count;
    requirements->text_bytes = plan.result_text_bytes;
    return ASTRA_OK;
}

static void update_lines(AstraTextModel *model, uint32_t start, uint32_t end,
                         const char *replacement, uint32_t replacement_bytes,
                         const EditPlan *plan)
{
    uint32_t prefix = 0u;
    uint32_t suffix;
    uint32_t destination;
    int64_t delta = (int64_t)replacement_bytes - (end - start);

    while (prefix < model->_private_line_count &&
           line_offset(model, prefix) <= start)
        ++prefix;
    suffix = prefix;
    if (start != end)
        while (suffix < model->_private_line_count &&
               line_offset(model, suffix) <= end)
            ++suffix;
    destination = prefix + plan->inserted_newlines;
    if (destination > suffix) {
        for (uint32_t index = model->_private_line_count; index-- > suffix;) {
            uint32_t value = (uint32_t)((int64_t)line_offset(model, index) +
                                        delta);

            line_offset_set(model, destination + index - suffix, value);
        }
    } else {
        for (uint32_t index = suffix;
             index < model->_private_line_count; ++index) {
            uint32_t value = (uint32_t)((int64_t)line_offset(model, index) +
                                        delta);

            line_offset_set(model, destination + index - suffix, value);
        }
    }
    destination = prefix;
    for (uint32_t at = 0u; at < replacement_bytes; ++at)
        if (replacement[at] == '\n')
            line_offset_set(model, destination++, start + at + 1u);
}

AstraResult astra_text_model_replace(
    AstraTextModel *model, uint32_t start, uint32_t end,
    const char *replacement, uint32_t replacement_bytes)
{
    EditPlan plan;
    AstraResult result = make_plan(model, start, end, replacement,
                                   replacement_bytes, &plan);
    TextPiece *table;
    uint32_t tail;

    if (result != ASTRA_OK)
        return result;
    if (plan.content_required > model->_private_content_capacity ||
        plan.metadata_required > metadata_limit(model))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    if (replacement_bytes != 0u)
        memmove(model->_private_content + model->_private_content_used,
                replacement, replacement_bytes);
    update_lines(model, start, end, replacement, replacement_bytes, &plan);
    table = pieces(model);
    tail = model->_private_piece_count - plan.replace_after;
    memmove(table + plan.replace_first + plan.piece_count,
            table + plan.replace_after, (size_t)tail * sizeof(*table));
    if (plan.piece_count != 0u)
        memcpy(table + plan.replace_first, plan.pieces,
               (size_t)plan.piece_count * sizeof(*table));
    model->_private_content_used = plan.content_required;
    model->_private_piece_count = plan.result_piece_count;
    model->_private_line_count = plan.result_line_count;
    model->_private_text_bytes = plan.result_text_bytes;
    model->_private_anchor = start + replacement_bytes;
    model->_private_focus = model->_private_anchor;
    model->_private_generation = next_generation(model->_private_generation);
    return ASTRA_OK;
}

AstraResult astra_text_model_copy(
    const AstraTextModel *model, uint32_t start, uint32_t end,
    char *output, uint32_t capacity, uint32_t *bytes)
{
    const TextPiece *table;
    uint32_t index;
    uint32_t inside;
    uint32_t remaining;
    uint32_t written = 0u;

    if (!model_valid(model) || start > end ||
        end > model->_private_text_bytes || bytes == NULL ||
        (output == NULL && capacity != 0u) ||
        !boundary(model, start) || !boundary(model, end) ||
        !locate(model, start, &index, &inside))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *bytes = end - start;
    if (output != NULL &&
        (ranges_overlap(output, *bytes, model->_private_content,
                        model->_private_content_capacity) ||
         ranges_overlap(output, *bytes, model->_private_metadata,
                        model->_private_metadata_capacity)))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (*bytes > capacity || (*bytes != 0u && output == NULL))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    table = const_pieces(model);
    remaining = *bytes;
    while (remaining != 0u) {
        const TextPiece *piece;
        uint32_t take;

        if (index >= model->_private_piece_count)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        piece = &table[index];
        if (!piece_valid(model, piece) || inside >= piece->length)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        take = piece->length - inside;
        if (take > remaining) take = remaining;
        memcpy(output + written,
               model->_private_content + piece->offset + inside, take);
        written += take;
        remaining -= take;
        ++index;
        inside = 0u;
    }
    return ASTRA_OK;
}

AstraResult astra_text_model_read(
    const AstraTextModel *model, uint32_t offset,
    const char **text, uint32_t *bytes)
{
    uint32_t index;
    uint32_t inside;

    if (!model_valid(model) || text == NULL || bytes == NULL ||
        !boundary(model, offset) ||
        !locate(model, offset, &index, &inside))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (index == model->_private_piece_count) {
        *text = NULL;
        *bytes = 0u;
        return ASTRA_OK;
    }
    if (!piece_valid(model, &const_pieces(model)[index]))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *text = (const char *)model->_private_content +
            const_pieces(model)[index].offset + inside;
    *bytes = const_pieces(model)[index].length - inside;
    return ASTRA_OK;
}

AstraResult astra_text_model_scalar_advance(const AstraTextModel *model,
                                            uint32_t *offset)
{
    const char *text;
    uint32_t bytes;
    uint32_t relative = 0u;
    AstraResult result;

    if (offset == NULL)
        return ASTRA_ERROR_INVALID_ARGUMENT;
    result = astra_text_model_read(model, *offset, &text, &bytes);
    if (result != ASTRA_OK)
        return result;
    if (bytes == 0u)
        return ASTRA_ERROR_NOT_PRESENT;
    if (!astra_utf8_scalar_advance(text, bytes, &relative))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *offset += relative;
    return ASTRA_OK;
}

AstraResult astra_text_model_scalar_retreat(const AstraTextModel *model,
                                            uint32_t *offset)
{
    const TextPiece *table;
    uint32_t index;
    uint32_t inside;
    uint32_t relative;

    if (!model_valid(model) || offset == NULL || !boundary(model, *offset) ||
        !locate(model, *offset, &index, &inside))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (*offset == 0u)
        return ASTRA_ERROR_NOT_PRESENT;
    table = const_pieces(model);
    if (inside == 0u) {
        if (index == 0u)
            return ASTRA_ERROR_INVALID_ARGUMENT;
        --index;
        if (!piece_valid(model, &table[index]))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        relative = table[index].length;
    } else {
        if (index >= model->_private_piece_count ||
            !piece_valid(model, &table[index]))
            return ASTRA_ERROR_INVALID_ARGUMENT;
        relative = inside;
    }
    inside = relative;
    if (!astra_utf8_scalar_retreat(model->_private_content +
                                       table[index].offset,
                                   table[index].length, &inside))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    *offset -= relative - inside;
    return ASTRA_OK;
}

AstraResult astra_text_model_get_line(
    const AstraTextModel *model, uint32_t line_index, AstraTextLine *line)
{
    uint8_t last;

    if (!model_valid(model) || line == NULL || line->size < sizeof(*line) ||
        !astra_words_zero(line->reserved, 4u))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    if (line_index >= model->_private_line_count)
        return ASTRA_ERROR_NOT_PRESENT;
    *line = (AstraTextLine)ASTRA_TEXT_LINE_INIT;
    line->start = line_offset(model, line_index);
    line->end = line_index + 1u < model->_private_line_count ?
        line_offset(model, line_index + 1u) : model->_private_text_bytes;
    line->content_end = line->end;
    if (line->content_end > line->start &&
        document_byte(model, line->content_end - 1u, &last) && last == '\n')
        --line->content_end;
    return ASTRA_OK;
}

AstraResult astra_text_model_move_arenas(
    AstraTextModel *model, void *content_arena, uint32_t content_arena_bytes,
    void *metadata_arena, uint32_t metadata_arena_bytes)
{
    AstraTextModel moved = ASTRA_TEXT_MODEL_INIT;
    uint64_t metadata_required;
    uint32_t copied;
    uint32_t generation;

    if (!model_valid(model) ||
        (content_arena == NULL && content_arena_bytes != 0u) ||
        content_arena_bytes < model->_private_text_bytes ||
        metadata_arena == NULL || ((uintptr_t)metadata_arena & 3u) != 0u ||
        ranges_overlap(content_arena, content_arena_bytes,
                       metadata_arena, metadata_arena_bytes) ||
        ranges_overlap(content_arena, content_arena_bytes,
                       model->_private_content,
                       model->_private_content_capacity) ||
        ranges_overlap(content_arena, content_arena_bytes,
                       model->_private_metadata,
                       model->_private_metadata_capacity) ||
        ranges_overlap(metadata_arena, metadata_arena_bytes,
                       model->_private_content,
                       model->_private_content_capacity) ||
        ranges_overlap(metadata_arena, metadata_arena_bytes,
                       model->_private_metadata,
                       model->_private_metadata_capacity))
        return ASTRA_ERROR_INVALID_ARGUMENT;
    metadata_required =
        (model->_private_text_bytes == 0u ? 0u : sizeof(TextPiece)) +
        (uint64_t)model->_private_line_count * sizeof(uint32_t);
    if (metadata_required > (metadata_arena_bytes & ~UINT32_C(3)))
        return ASTRA_ERROR_BUFFER_TOO_SMALL;
    if (astra_text_model_copy(model, 0u, model->_private_text_bytes,
                              content_arena, content_arena_bytes,
                              &copied) != ASTRA_OK)
        return ASTRA_ERROR_INVALID_ARGUMENT;

    moved._private_content = content_arena;
    moved._private_content_capacity = content_arena_bytes;
    moved._private_content_used = model->_private_text_bytes;
    moved._private_metadata = metadata_arena;
    moved._private_metadata_capacity = metadata_arena_bytes;
    moved._private_piece_count = model->_private_text_bytes == 0u ? 0u : 1u;
    moved._private_line_count = model->_private_line_count;
    moved._private_text_bytes = model->_private_text_bytes;
    moved._private_anchor = model->_private_anchor;
    moved._private_focus = model->_private_focus;
    moved._private_generation = next_generation(model->_private_generation);
    if (moved._private_piece_count != 0u)
        pieces(&moved)[0] = (TextPiece){0u, moved._private_text_bytes};
    for (uint32_t line = 0u; line < moved._private_line_count; ++line)
        line_offset_set(&moved, line, line_offset(model, line));

    generation = moved._private_generation;
    if (model->_private_content_used != 0u)
        memset(model->_private_content, 0, model->_private_content_used);
    memset(model->_private_metadata, 0,
           (size_t)model->_private_piece_count * sizeof(TextPiece));
    memset(model->_private_metadata + metadata_limit(model) -
               model->_private_line_count * sizeof(uint32_t),
           0, (size_t)model->_private_line_count * sizeof(uint32_t));
    *model = moved;
    model->_private_generation = generation;
    return ASTRA_OK;
}

void astra_text_model_dispose(AstraTextModel *model)
{
    if (model == NULL)
        return;
    if (model_valid(model)) {
        if (model->_private_content_used != 0u)
            memset(model->_private_content, 0,
                   model->_private_content_used);
        memset(model->_private_metadata, 0,
               (size_t)model->_private_piece_count * sizeof(TextPiece));
        memset(model->_private_metadata + metadata_limit(model) -
                   model->_private_line_count * sizeof(uint32_t),
               0, (size_t)model->_private_line_count * sizeof(uint32_t));
    }
    *model = (AstraTextModel)ASTRA_TEXT_MODEL_INIT;
}
