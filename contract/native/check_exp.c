/**
 * @file    check_exp.c
 * @copyright defined in aergo/LICENSE.txt
 */

#include "common.h"

#include "ast_id.h"
#include "ast_blk.h"

#include "check_exp.h"

static int
exp_check_id(check_t *check, ast_exp_t *exp)
{
    ast_id_t *id = NULL;

    ASSERT1(is_id_exp(exp), exp->kind);

    if (check->aq_id != NULL) {
        id = id_search_fld(check->aq_id, exp->u_id.name);
    }
    else {
        if (check->fn_id != NULL)
            id = id_search_param(check->fn_id, exp->u_id.name);

        if (id == NULL) {
            id = id_search_name(check->blk, exp->num, exp->u_id.name);

            if (id != NULL && is_contract_id(id))
                id = id_search_fld(id, exp->u_id.name);
        }
    }

    if (id == NULL)
        RETURN(ERROR_UNDEFINED_ID, &exp->pos, exp->u_id.name);

    id->is_used = true;

    exp->id = id;
    meta_copy(&exp->meta, &id->meta);

    return NO_ERROR;
}

static int
exp_check_val(check_t *check, ast_exp_t *exp)
{
    ASSERT1(is_val_exp(exp), exp->kind);

    switch (exp->u_val.val.kind) {
    case VAL_NULL:
        meta_set_ref(&exp->meta);
        break;
    case VAL_BOOL:
        meta_set_bool(&exp->meta);
        break;
    case VAL_INT:
        meta_set_untyped(&exp->meta, TYPE_INT64);
        break;
    case VAL_FP:
        meta_set_untyped(&exp->meta, TYPE_DOUBLE);
        break;
    case VAL_STR:
        meta_set_string(&exp->meta);
        break;
    default:
        ASSERT1(!"invalid value", exp->u_val.val.kind);
    }

    return NO_ERROR;
}

static int
exp_check_type(check_t *check, ast_exp_t *exp)
{
    ASSERT1(is_type_exp(exp), exp->kind);

    if (exp->u_type.type == TYPE_STRUCT) {
        ast_id_t *id;

        ASSERT(exp->u_type.name != NULL);
        ASSERT(exp->u_type.k_exp == NULL);
        ASSERT(exp->u_type.v_exp == NULL);

        if (check->aq_id != NULL)
            id = id_search_fld(check->aq_id, exp->u_type.name);
        else
            id = id_search_name(check->blk, exp->num, exp->u_type.name);

        if (id == NULL || (!is_struct_id(id) && !is_contract_id(id)))
            RETURN(ERROR_UNDEFINED_TYPE, &exp->pos, exp->u_type.name);

        id->is_used = true;

        exp->id = id;
        meta_copy(&exp->meta, &id->meta);
    }
    else if (exp->u_type.type == TYPE_MAP) {
        ast_exp_t *k_exp, *v_exp;
        meta_t *k_meta, *v_meta;

        ASSERT1(exp->u_type.name == NULL, exp->u_type.name);
        ASSERT(exp->u_type.k_exp != NULL);
        ASSERT(exp->u_type.v_exp != NULL);

        k_exp = exp->u_type.k_exp;
        k_meta = &k_exp->meta;

        CHECK(exp_check_type(check, k_exp));

        if (!is_comparable_meta(k_meta))
            RETURN(ERROR_INVALID_KEY_TYPE, &k_exp->pos, meta_to_str(k_meta));

        v_exp = exp->u_type.v_exp;
        v_meta = &v_exp->meta;

        CHECK(exp_check_type(check, v_exp));

        ASSERT(!is_tuple_meta(v_meta));
        meta_set_map(&exp->meta, k_meta, v_meta);
    }
    else {
        ASSERT1(exp->u_type.name == NULL, exp->u_type.name);
        ASSERT(exp->u_type.k_exp == NULL);
        ASSERT(exp->u_type.v_exp == NULL);

        meta_set(&exp->meta, exp->u_type.type);
    }

    return NO_ERROR;
}

static int
exp_check_array(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *id_exp;
    meta_t *id_meta;
    ast_exp_t *idx_exp;
    meta_t *idx_meta;

    ASSERT1(is_array_exp(exp), exp->kind);
    ASSERT(exp->u_arr.id_exp != NULL);

    id_exp = exp->u_arr.id_exp;
    id_meta = &id_exp->meta;

    CHECK(exp_check(check, id_exp));

    if (!is_array_meta(id_meta) && !is_map_meta(id_meta))
        RETURN(ERROR_INVALID_SUBSCRIPT, &id_exp->pos);

    exp->id = id_exp->id;

    idx_exp = exp->u_arr.idx_exp;
    idx_meta = &idx_exp->meta;

    CHECK(exp_check(check, idx_exp));

    if (is_map_meta(id_meta)) {
        CHECK(meta_check(id_meta->u_map.k_meta, idx_meta));

        meta_copy(&exp->meta, id_meta->u_map.v_meta);
    }
    else {
        // TODO: check index value if possible
        if (!is_integer_meta(idx_meta))
            RETURN(ERROR_INVALID_SIZE_VAL, &idx_exp->pos,
                   meta_to_str(idx_meta));

        meta_copy(&exp->meta, id_meta);
    }

    return NO_ERROR;
}

static int
exp_op_eval_const(ast_exp_t *exp, type_t type)
{
    op_kind_t op = exp->u_op.kind;
    ast_exp_t *l_exp = exp->u_op.l_exp;
    ast_exp_t *r_exp = exp->u_op.r_exp;
    value_t *r_val = NULL;

    ASSERT1(is_val_exp(l_exp), l_exp->kind);

    if (r_exp != NULL) {
        ASSERT1(is_val_exp(r_exp), r_exp->kind);

        r_val = &r_exp->u_val.val;

        if ((op == OP_DIV || op == OP_MOD) && is_zero_val(r_val))
            RETURN(ERROR_DIVIDE_BY_ZERO, &r_exp->pos);
    }

    value_eval(op, &exp->u_val.val, &l_exp->u_val.val, r_val);

    exp->kind = EXP_VAL;
    meta_set_untyped(&exp->meta, type);

    return NO_ERROR;
}

static int
exp_check_op_arith(check_t *check, ast_exp_t *exp)
{
    op_kind_t op = exp->u_op.kind;
    ast_exp_t *l_exp, *r_exp;
    meta_t *l_meta, *r_meta;

    ASSERT(exp->u_op.l_exp != NULL);
    ASSERT(exp->u_op.r_exp != NULL);

    l_exp = exp->u_op.l_exp;
    l_meta = &l_exp->meta;

    CHECK(exp_check(check, l_exp));

    if (op == OP_ADD) {
        if (!is_numeric_meta(l_meta) && !is_string_meta(l_meta))
            RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));
    }
    else if (op == OP_MOD) {
        if (!is_integer_meta(l_meta))
            RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));
    }
    else if (!is_numeric_meta(l_meta)) {
        RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));
    }

    r_exp = exp->u_op.r_exp;
    r_meta = &r_exp->meta;

    CHECK(exp_check(check, r_exp));

    if (!meta_equals(l_meta, r_meta))
        RETURN(ERROR_MISMATCHED_TYPE, &exp->pos, meta_to_str(l_meta),
               meta_to_str(r_meta));

    meta_merge(&exp->meta, l_meta, r_meta);

    if (is_untyped_meta(l_meta) && is_untyped_meta(r_meta))
        exp_op_eval_const(exp, MAX(l_meta->type, r_meta->type));

    return NO_ERROR;
}

static int
exp_check_op_bit(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *l_exp, *r_exp;
    meta_t *l_meta, *r_meta;

    ASSERT(exp->u_op.l_exp != NULL);
    ASSERT(exp->u_op.r_exp != NULL);

    l_exp = exp->u_op.l_exp;
    l_meta = &l_exp->meta;

    CHECK(exp_check(check, l_exp));

    if (!is_integer_meta(l_meta))
        RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));

    r_exp = exp->u_op.r_exp;
    r_meta = &r_exp->meta;

    CHECK(exp_check(check, r_exp));

    if (!is_integer_meta(r_meta))
        RETURN(ERROR_INVALID_OP_TYPE, &r_exp->pos, meta_to_str(r_meta));

    meta_copy(&exp->meta, l_meta);

    if (is_untyped_meta(l_meta) && is_untyped_meta(r_meta))
        exp_op_eval_const(exp, l_meta->type);

    return NO_ERROR;
}

static int
exp_check_op_cmp(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *l_exp, *r_exp;
    meta_t *l_meta, *r_meta;

    ASSERT(exp->u_op.l_exp != NULL);
    ASSERT(exp->u_op.r_exp != NULL);

    l_exp = exp->u_op.l_exp;
    l_meta = &l_exp->meta;

    CHECK(exp_check(check, l_exp));

    r_exp = exp->u_op.r_exp;
    r_meta = &r_exp->meta;

    CHECK(exp_check(check, r_exp));

    /* XXX: comparable check */
    if (!meta_equals(l_meta, r_meta))
        RETURN(ERROR_MISMATCHED_TYPE, &r_exp->pos,
               meta_to_str(l_meta), meta_to_str(r_meta));

    meta_set_bool(&exp->meta);

    if (is_untyped_meta(l_meta) && is_untyped_meta(r_meta))
        exp_op_eval_const(exp, TYPE_BOOL);

    return NO_ERROR;
}

static int
exp_check_op_unary(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *l_exp;
    meta_t *l_meta;

    ASSERT(exp->u_op.l_exp != NULL);
    ASSERT(exp->u_op.r_exp == NULL);

    l_exp = exp->u_op.l_exp;
    l_meta = &l_exp->meta;

    CHECK(exp_check(check, l_exp));

    switch (exp->u_op.kind) {
    case OP_INC:
    case OP_DEC:
        /* XXX: need position information (prefix or postfix) */
        if (!is_usable_lval(l_exp))
            RETURN(ERROR_INVALID_LVALUE, &l_exp->pos);

        if (!is_integer_meta(l_meta))
            RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));

        meta_copy(&exp->meta, l_meta);
        break;

    case OP_NEG:
        if (!is_numeric_meta(l_meta))
            RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));

        meta_copy(&exp->meta, l_meta);

        if (is_untyped_meta(l_meta))
            exp_op_eval_const(exp, l_meta->type);
        break;

    case OP_NOT:
        if (!is_bool_meta(l_meta))
            RETURN(ERROR_INVALID_OP_TYPE, &l_exp->pos, meta_to_str(l_meta));

        meta_copy(&exp->meta, l_meta);

        if (is_untyped_meta(l_meta))
            exp_op_eval_const(exp, TYPE_BOOL);
        break;

    default:
        ASSERT1(!"invalid operator", exp->u_op.kind);
    }

    return NO_ERROR;
}

static int
exp_check_op_bool_cmp(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *l_exp, *r_exp;
    meta_t *l_meta, *r_meta;

    ASSERT(exp->u_op.l_exp != NULL);
    ASSERT(exp->u_op.r_exp != NULL);

    l_exp = exp->u_op.l_exp;
    l_meta = &l_exp->meta;

    CHECK(exp_check(check, l_exp));

    if (!is_bool_meta(l_meta))
        RETURN(ERROR_INVALID_COND_TYPE, &l_exp->pos, meta_to_str(l_meta));

    r_exp = exp->u_op.r_exp;
    r_meta = &r_exp->meta;

    CHECK(exp_check(check, r_exp));

    if (!is_bool_meta(r_meta))
        RETURN(ERROR_INVALID_COND_TYPE, &r_exp->pos, meta_to_str(r_meta));

    meta_set_bool(&exp->meta);

    return NO_ERROR;
}

static int
exp_check_op_assign(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *l_exp;
    meta_t *l_meta;
    ast_exp_t *r_exp;
    meta_t *r_meta;

    ASSERT(exp->u_op.l_exp != NULL);
    ASSERT(exp->u_op.r_exp != NULL);

    l_exp = exp->u_op.l_exp;
    l_meta = &l_exp->meta;

    CHECK(exp_check(check, l_exp));

    r_exp = exp->u_op.r_exp;
    r_meta = &r_exp->meta;

    CHECK(exp_check(check, r_exp));

    if (is_tuple_exp(l_exp)) {
        int i;
        array_t *var_exps = l_exp->u_tup.exps;
        array_t *val_exps = r_exp->u_tup.exps;

        if (!is_tuple_exp(r_exp))
            RETURN(ERROR_MISMATCHED_ELEM_CNT, &r_exp->pos,
                   array_size(var_exps), 1);

        if (array_size(var_exps) != array_size(val_exps))
            RETURN(ERROR_MISMATCHED_ELEM_CNT, &r_exp->pos,
                   array_size(var_exps), array_size(val_exps));

        for (i = 0; i < array_size(var_exps); i++) {
            ast_exp_t *var_exp = array_item(var_exps, i, ast_exp_t);
            ast_exp_t *val_exp = array_item(val_exps, i, ast_exp_t);

            if (!is_usable_lval(var_exp))
                RETURN(ERROR_INVALID_LVALUE, &var_exp->pos);

            if (!meta_equals(&var_exp->meta, &val_exp->meta))
                RETURN(ERROR_MISMATCHED_TYPE, &val_exp->pos,
                       meta_to_str(&var_exp->meta), meta_to_str(&val_exp->meta));
        }
    }
    else {
        if (!is_usable_lval(l_exp))
            RETURN(ERROR_INVALID_LVALUE, &l_exp->pos);

        if (!meta_equals(l_meta, r_meta))
            RETURN(ERROR_MISMATCHED_TYPE, &r_exp->pos,
                   meta_to_str(l_meta), meta_to_str(r_meta));
    }

    meta_merge(&exp->meta, l_meta, r_meta);

    if (is_val_exp(r_exp) &&
        !value_check_range(&r_exp->u_val.val, l_meta->type))
        RETURN(ERROR_NUMERIC_OVERFLOW, &r_exp->pos, meta_to_str(l_meta));

    return NO_ERROR;
}

static int
exp_check_op(check_t *check, ast_exp_t *exp)
{
    ASSERT1(is_op_exp(exp), exp->kind);

    switch (exp->u_op.kind) {
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
        return exp_check_op_arith(check, exp);

    case OP_BIT_AND:
    case OP_BIT_OR:
    case OP_BIT_XOR:
    case OP_RSHIFT:
    case OP_LSHIFT:
        return exp_check_op_bit(check, exp);

    case OP_EQ:
    case OP_NE:
    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
        return exp_check_op_cmp(check, exp);

    case OP_INC:
    case OP_DEC:
    case OP_NOT:
    case OP_NEG:
        return exp_check_op_unary(check, exp);

    case OP_AND:
    case OP_OR:
        return exp_check_op_bool_cmp(check, exp);

    case OP_ASSIGN:
        return exp_check_op_assign(check, exp);

    default:
        ASSERT1(!"invalid operator", exp->u_op.kind);
    }

    return NO_ERROR;
}

static int
exp_check_access(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *id_exp, *fld_exp;
    meta_t *id_meta, *fld_meta;
    ast_id_t *id;

    ASSERT1(is_access_exp(exp), exp->kind);

    id_exp = exp->u_acc.id_exp;
    id_meta = &id_exp->meta;

    CHECK(exp_check(check, id_exp));

    id = id_exp->id;
    if (id == NULL || is_tuple_meta(id_meta))
        RETURN(ERROR_NOT_ACCESSIBLE_EXP, &id_exp->pos);

    if (is_var_id(id)) {
        ast_id_t *type_id = id->u_var.type_exp->id;

        if (type_id == NULL ||
            (!is_struct_id(type_id) && !is_contract_id(type_id)))
            RETURN(ERROR_NOT_ACCESSIBLE_EXP, &id_exp->pos);

        id = type_id;
    }
    else if (is_func_id(id) &&
             !is_struct_meta(id_meta) && !is_ref_meta(id_meta)) {
        RETURN(ERROR_NOT_ACCESSIBLE_EXP, &id_exp->pos);
    }

    fld_exp = exp->u_acc.fld_exp;
    fld_meta = &fld_exp->meta;

    check->aq_id = id;

    if (exp_check(check, fld_exp) == NO_ERROR) {
        exp->id = fld_exp->id;
        meta_copy(&exp->meta, fld_meta);
    }

    check->aq_id = NULL;

    return NO_ERROR;
}

static int
exp_check_call(check_t *check, ast_exp_t *exp)
{
    int i;
    ast_exp_t *id_exp;
    array_t *param_exps;
    ast_id_t *id;
    array_t *param_ids;

    ASSERT1(is_call_exp(exp), exp->kind);

    id_exp = exp->u_call.id_exp;
    param_exps = exp->u_call.param_exps;

    if (strcmp(id_exp->u_id.name, "map") == 0) {
        if (param_exps != NULL) {
            ast_exp_t *param_exp;

            ASSERT1(array_size(param_exps) == 1, array_size(param_exps));
            param_exp = array_item(param_exps, 0, ast_exp_t);

            CHECK(exp_check(check, param_exp));
            ASSERT1(is_integer_meta(&param_exp->meta), param_exp->meta.type);
        }

        meta_set_untyped(&exp->meta, TYPE_MAP);

        return NO_ERROR;
    }

    CHECK(exp_check(check, id_exp));

    id = id_exp->id;
    if (id == NULL || !is_func_id(id))
        RETURN(ERROR_NOT_CALLABLE_EXP, &id_exp->pos);

    param_ids = id->u_func.param_ids;

    if (array_size(param_ids) != array_size(param_exps))
        RETURN(ERROR_MISMATCHED_COUNT, &id_exp->pos,
               array_size(param_ids), array_size(param_exps));

    for (i = 0; i < array_size(param_exps); i++) {
        ast_id_t *param_id = array_item(param_ids, i, ast_id_t);
        ast_exp_t *param_exp = array_item(param_exps, i, ast_exp_t);

        CHECK(exp_check(check, param_exp));

        if (!meta_equals(&param_id->meta, &param_exp->meta))
            RETURN(ERROR_MISMATCHED_TYPE, &param_exp->pos,
                   meta_to_str(&param_id->meta), meta_to_str(&param_exp->meta));
    }

    exp->id = id;
    meta_copy(&exp->meta, &id->meta);

    return NO_ERROR;
}

static int
exp_check_sql(check_t *check, ast_exp_t *exp)
{
    ASSERT1(is_sql_exp(exp), exp->kind);
    ASSERT(exp->u_sql.sql != NULL);

    switch (exp->u_sql.kind) {
    case SQL_QUERY:
        // TODO: need column meta
        break;

    case SQL_INSERT:
    case SQL_UPDATE:
    case SQL_DELETE:
        meta_set_int32(&exp->meta);
        break;

    default:
        ASSERT1(!"invalid sql", exp->u_sql.kind);
    }

    return NO_ERROR;
}

static int
exp_check_ternary(check_t *check, ast_exp_t *exp)
{
    ast_exp_t *pre_exp, *in_exp, *post_exp;
    meta_t *pre_meta, *in_meta, *post_meta;

    ASSERT1(is_ternary_exp(exp), exp->kind);
    ASSERT(exp->u_tern.pre_exp != NULL);
    ASSERT(exp->u_tern.in_exp != NULL);
    ASSERT(exp->u_tern.post_exp != NULL);

    pre_exp = exp->u_tern.pre_exp;
    pre_meta = &pre_exp->meta;

    CHECK(exp_check(check, pre_exp));

    if (!is_bool_meta(pre_meta))
        RETURN(ERROR_INVALID_COND_TYPE, &pre_exp->pos, meta_to_str(pre_meta));

    in_exp = exp->u_tern.in_exp;
    in_meta = &in_exp->meta;

    CHECK(exp_check(check, in_exp));

    post_exp = exp->u_tern.post_exp;
    post_meta = &post_exp->meta;

    CHECK(exp_check(check, post_exp));

    if (!meta_equals(in_meta, post_meta))
        RETURN(ERROR_MISMATCHED_TYPE, &post_exp->pos,
               meta_to_str(in_meta), meta_to_str(post_meta));

    meta_copy(&exp->meta, in_meta);

    return NO_ERROR;
}

static int
exp_check_tuple(check_t *check, ast_exp_t *exp)
{
    int i;
    array_t *exps = exp->u_tup.exps;

    ASSERT1(is_tuple_exp(exp), exp->kind);
    ASSERT(exps != NULL);

    for (i = 0; i < array_size(exps); i++) {
        ast_exp_t *item = array_item(exps, i, ast_exp_t);

        CHECK(exp_check(check, item));
    }

    meta_set_tuple(&exp->meta, exps);

    return NO_ERROR;
}

int
exp_check(check_t *check, ast_exp_t *exp)
{
    ASSERT(exp != NULL);

    switch (exp->kind) {
    case EXP_NULL:
        return NO_ERROR;

    case EXP_ID:
        return exp_check_id(check, exp);

    case EXP_VAL:
        return exp_check_val(check, exp);

    case EXP_TYPE:
        return exp_check_type(check, exp);

    case EXP_ARRAY:
        return exp_check_array(check, exp);

    case EXP_OP:
        return exp_check_op(check, exp);

    case EXP_ACCESS:
        return exp_check_access(check, exp);

    case EXP_CALL:
        return exp_check_call(check, exp);

    case EXP_SQL:
        return exp_check_sql(check, exp);

    case EXP_TERNARY:
        return exp_check_ternary(check, exp);

    case EXP_TUPLE:
        return exp_check_tuple(check, exp);

    default:
        ASSERT1(!"invalid expression", exp->kind);
    }

    return NO_ERROR;
}

/* end of check_exp.c */
