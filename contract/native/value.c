/**
 * @file    value.c
 * @copyright defined in aergo/LICENSE.txt
 */

#include "common.h"

#include "meta.h"

#include "value.h"

#define value_check_int(val, max)                                                        \
    (((val)->is_neg && (val)->i > (uint64_t)(max) + 1) ||                                \
     (!(val)->is_neg && (val)->i > (uint64_t)(max)))

#define value_check_uint(val, max)      ((val)->is_neg || (val)->i > (max))

#define value_eval_arith(op, x, y, res)                                                  \
    do {                                                                                 \
        ASSERT2((x)->kind == (y)->kind, (x)->kind, (y)->kind);                           \
                                                                                         \
        if (is_int_val(x))                                                               \
            value_set_int(res, int_val(x) op int_val(y));                                \
        else if (is_fp_val(x))                                                           \
            value_set_fp(res, fp_val(x) op fp_val(y));                                   \
        else if (is_str_val(x))                                                          \
            value_set_str((res), xstrcat(str_val(x), str_val(y)));                       \
        else                                                                             \
            ASSERT1(!"invalid value", (x)->kind);                                        \
    } while (0)

#define value_eval_cmp(op, x, y, res)                                                    \
    do {                                                                                 \
        bool v = false;                                                                \
                                                                                         \
        ASSERT2((x)->kind == (y)->kind, (x)->kind, (y)->kind);                           \
                                                                                         \
        if (is_bool_val(x))                                                              \
            v = bool_val(x) op bool_val(y);                                            \
        else if (is_int_val(x))                                                          \
            v = int_val(x) op int_val(y);                                              \
        else if (is_fp_val(x))                                                           \
            v = fp_val(x) op fp_val(y);                                                \
        else if (is_str_val(x))                                                          \
            v = strcmp(str_val(x), str_val(y)) op 0;                                   \
        else if (is_obj_val(x))                                                          \
            v = obj_val(x) op obj_val(y);                                              \
        else                                                                             \
            ASSERT1(!"invalid value", (x)->kind);                                        \
                                                                                         \
        value_set_bool((res), v);                                                      \
    } while (0)

#define value_eval_bit(op, x, y, res)                                                    \
    do {                                                                                 \
        ASSERT2((x)->kind == (y)->kind, (x)->kind, (y)->kind);                           \
                                                                                         \
        if (is_int_val(x))                                                               \
            value_set_int((res), int_val(x) op int_val(y));                              \
        else                                                                             \
            ASSERT1(!"invalid value", (x)->kind);                                        \
    } while (0)

#define value_eval_bool_cmp(op, x, y, res)                                               \
    do {                                                                                 \
        ASSERT2((x)->kind == (y)->kind, (x)->kind, (y)->kind);                           \
                                                                                         \
        if (is_bool_val(x))                                                              \
            value_set_bool((res), bool_val(x) op bool_val(y));                           \
        else                                                                             \
            ASSERT1(!"invalid value", (x)->kind);                                        \
    } while (0)

bool
value_check(value_t *val, meta_t *meta)
{
    /* assume that we've already checked meta */
    switch (val->kind) {
    case VAL_BOOL:
        ASSERT1(is_bool_type(meta), meta->type);
        break;

    case VAL_INT:
        ASSERT1(is_dec_family(meta), meta->type);
        if ((meta->type == TYPE_BYTE && value_check_uint(val, UINT8_MAX)) ||
            (meta->type == TYPE_INT8 && value_check_int(val, INT8_MAX)) ||
            (meta->type == TYPE_UINT8 && value_check_uint(val, UINT8_MAX)) ||
            (meta->type == TYPE_INT16 && value_check_int(val, INT16_MAX)) ||
            (meta->type == TYPE_UINT16 && value_check_uint(val, UINT16_MAX)) ||
            (meta->type == TYPE_INT32 && value_check_int(val, INT32_MAX)) ||
            (meta->type == TYPE_UINT32 && value_check_uint(val, UINT32_MAX)) ||
            (meta->type == TYPE_INT64 && value_check_int(val, INT64_MAX)) ||
            (meta->type == TYPE_UINT64 && val->is_neg))
            return false;
        break;

    case VAL_FP:
        ASSERT1(is_fp_family(meta), meta->type);
        if (meta->type == TYPE_FLOAT && val->d > FLT_MAX)
            return false;
        break;

    case VAL_STR:
        ASSERT1(is_string_type(meta), meta->type);
        break;

    case VAL_OBJ:
        ASSERT1(is_obj_family(meta), meta->type);
        break;

    case VAL_ADDR:
        ASSERT1(is_string_type(meta) || is_struct_type(meta) || is_tuple_type(meta),
                meta->type);
        break;

    default:
        ASSERT1(!"invalid value", val->kind);
    }

    return true;
}

static void
value_add(value_t *x, value_t *y, value_t *res)
{
    value_eval_arith(+, x, y, res);
}

static void
value_sub(value_t *x, value_t *y, value_t *res)
{
    value_eval_arith(-, x, y, res);
}

static void
value_mul(value_t *x, value_t *y, value_t *res)
{
    value_eval_arith(*, x, y, res);
}

static void
value_div(value_t *x, value_t *y, value_t *res)
{
    if (is_int_val(x))
        ASSERT(y->i != 0);
    else if (is_fp_val(x))
        ASSERT(y->d != 0.0f);

    value_eval_arith(/, x, y, res);
}

static void
value_mod(value_t *x, value_t *y, value_t *res)
{
    if (is_int_val(x)) {
        ASSERT(y->i != 0);
        value_set_int(res, x->i % y->i);
    }
    else {
        ASSERT1(!"invalid value", res->kind);
    }
}

static void
value_cmp_eq(value_t *x, value_t *y, value_t *res)
{
    value_eval_cmp(==, x, y, res);
}

static void
value_cmp_ne(value_t *x, value_t *y, value_t *res)
{
    value_eval_cmp(!=, x, y, res);
}

static void
value_cmp_lt(value_t *x, value_t *y, value_t *res)
{
    value_eval_cmp(<, x, y, res);
}

static void
value_cmp_gt(value_t *x, value_t *y, value_t *res)
{
    value_eval_cmp(>, x, y, res);
}

static void
value_cmp_le(value_t *x, value_t *y, value_t *res)
{
    value_eval_cmp(<=, x, y, res);
}

static void
value_cmp_ge(value_t *x, value_t *y, value_t *res)
{
    value_eval_cmp(>=, x, y, res);
}

static void
value_bit_and(value_t *x, value_t *y, value_t *res)
{
    value_eval_bit(&, x, y, res);
}

static void
value_bit_or(value_t *x, value_t *y, value_t *res)
{
    value_eval_bit(|, x, y, res);
}

static void
value_bit_xor(value_t *x, value_t *y, value_t *res)
{
    value_eval_bit(^, x, y, res);
}

static void
value_shift_l(value_t *x, value_t *y, value_t *res)
{
    value_eval_bit(<<, x, y, res);
}

static void
value_shift_r(value_t *x, value_t *y, value_t *res)
{
    value_eval_bit(>>, x, y, res);
}

static void
value_neg(value_t *x, value_t *y, value_t *res)
{
    ASSERT(y == NULL);

    if (is_int_val(x))
        value_set_int(res, int_val(x));
    else if (is_fp_val(x))
        value_set_fp(res, fp_val(x));
    else
        ASSERT1(!"invalid value", x->kind);

    value_set_neg(res, !x->is_neg);
}

static void
value_not(value_t *x, value_t *y, value_t *res)
{
    ASSERT(y == NULL);

    if (is_bool_val(x))
        value_set_bool(res, !x->b);
    else
        ASSERT1(!"invalid value", x->kind);
}

static void
value_and(value_t *x, value_t *y, value_t *res)
{
    value_eval_bool_cmp(&&, x, y, res);
}

static void
value_or(value_t *x, value_t *y, value_t *res)
{
    value_eval_bool_cmp(||, x, y, res);
}

eval_fn_t eval_fntab_[OP_CF_MAX + 1] = {
    value_add,
    value_sub,
    value_mul,
    value_div,
    value_mod,
    value_cmp_eq,
    value_cmp_ne,
    value_cmp_lt,
    value_cmp_gt,
    value_cmp_le,
    value_cmp_ge,
    value_bit_and,
    value_bit_or,
    value_bit_xor,
    value_shift_l,
    value_shift_r,
    value_neg,
    value_not,
    value_and,
    value_or
};

void
value_eval(op_kind_t op, value_t *x, value_t *y, value_t *res)
{
    ASSERT1(op >= OP_ADD && op <= OP_CF_MAX, op);

    eval_fntab_[op](x, y, res);
}

static void
value_cast_to_bool(value_t *val)
{
    switch (val->kind) {
    case VAL_BOOL:
        break;

    case VAL_INT:
        value_set_bool(val, val->i != 0);
        value_set_neg(val, false);
        break;

    case VAL_FP:
        value_set_bool(val, val->d != 0.0);
        value_set_neg(val, false);
        break;

    case VAL_STR:
        value_set_bool(val, str_val(val) == NULL || strcmp(str_val(val), "false") == 0);
        break;

    default:
        ASSERT1(!"invalid value", val->kind);
    }
}

static void
value_cast_to_int(value_t *val)
{
    uint64_t i = 0;

    switch (val->kind) {
    case VAL_BOOL:
        value_set_int(val, val->b ? 1 : 0);
        break;

    case VAL_INT:
        break;

    case VAL_FP:
        value_set_int(val, (uint64_t)val->d);
        break;

    case VAL_STR:
        if (val->s != NULL) {
            if (val->s[0] == '-') {
                sscanf(val->s + 1, "%"SCNu64, &i);
                value_set_neg(val, true);
            }
            else {
                sscanf(val->s, "%"SCNu64, &i);
            }
        }
        value_set_int(val, i);
        break;

    default:
        ASSERT1(!"invalid value", val->kind);
    }
}

static void
value_cast_to_fp(value_t *val)
{
    double d;

    switch (val->kind) {
    case VAL_BOOL:
        value_set_fp(val, bool_val(val) ? 1.0 : 0.0);
        break;

    case VAL_INT:
        value_set_fp(val, (double)val->i);
        break;

    case VAL_FP:
        break;

    case VAL_STR:
        sscanf(val->s, "%lf", &d);
        value_set_fp(val, d);
        break;

    default:
        ASSERT1(!"invalid value", val->kind);
    }
}

static void
value_cast_to_str(value_t *val)
{
    char buf[256];

    switch (val->kind) {
    case VAL_BOOL:
        value_set_str(val, val->b ? xstrdup("true") : xstrdup("false"));
        break;

    case VAL_INT:
        snprintf(buf, sizeof(buf), "%"PRIu64, int_val(val));
        value_set_str(val, xstrdup(buf));
        break;

    case VAL_FP:
        snprintf(buf, sizeof(buf), "%lf", fp_val(val));
        value_set_str(val, xstrdup(buf));
        break;

    case VAL_STR:
        break;

    default:
        ASSERT1(!"invalid value", val->kind);
    }
}

void
value_cast(value_t *val, meta_t *to)
{
    if (is_bool_type(to))
        value_cast_to_bool(val);
    else if (is_dec_family(to))
        value_cast_to_int(val);
    else if (is_fp_family(to))
        value_cast_to_fp(val);
    else if (is_string_type(to))
        value_cast_to_str(val);
    else
        ASSERT1(!"invalid type", to->type);
}

int
value_cmp(value_t *x, value_t *y)
{
    ASSERT2(x->kind == y->kind, x->kind, y->kind);

    switch (x->kind) {
    case VAL_BOOL:
        return bool_val(x) == bool_val(y) ? 0 : (bool_val(x) > bool_val(y) ? 1 : -1);

    case VAL_INT:
        return int_val(x) == int_val(y) ? 0 : (int_val(x) > int_val(y) ? 1 : -1);

    case VAL_FP:
        return fp_val(x) == fp_val(y) ? 0 : (fp_val(x) > fp_val(y) ? 1 : -1);

    case VAL_STR:
        return strcmp(str_val(x), str_val(y));

    default:
        ASSERT1(!"invalid value", x->kind);
    }

    return 0;
}

/* end of value.c */