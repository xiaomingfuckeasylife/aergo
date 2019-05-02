/**
 * @file    ast_stmt.c
 * @copyright defined in aergo/LICENSE.txt
 */

#include "common.h"

#include "ast_id.h"
#include "ast_exp.h"
#include "ast_blk.h"

#include "ast_stmt.h"

static ast_stmt_t *
ast_stmt_new(stmt_kind_t kind, src_pos_t *pos)
{
    ast_stmt_t *stmt = xcalloc(sizeof(ast_stmt_t));

    ast_node_init(stmt, *pos);

    stmt->kind = kind;

    return stmt;
}

ast_stmt_t *
stmt_new_null(src_pos_t *pos)
{
    return ast_stmt_new(STMT_NULL, pos);
}

ast_stmt_t *
stmt_new_id(ast_id_t *id, src_pos_t *pos)
{
    ast_stmt_t *stmt;

    if (id == NULL)
        /* The "id" may be null because of grammar error recovery */
        return NULL;

    stmt = ast_stmt_new(STMT_ID, pos);
    stmt->u_id.id = id;

    return stmt;
}

ast_stmt_t *
stmt_new_exp(ast_exp_t *exp, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_EXP, pos);

    stmt->u_exp.exp = exp;

    return stmt;
}

ast_stmt_t *
stmt_new_assign(ast_exp_t *l_exp, ast_exp_t *r_exp, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_ASSIGN, pos);

    stmt->u_assign.l_exp = l_exp;
    stmt->u_assign.r_exp = r_exp;

    return stmt;
}

ast_stmt_t *
stmt_new_if(ast_exp_t *cond_exp, ast_blk_t *if_blk, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_IF, pos);

    stmt->u_if.cond_exp = cond_exp;
    stmt->u_if.if_blk = if_blk;
    stmt->u_if.else_blk = NULL;
    vector_init(&stmt->u_if.elif_stmts);

    return stmt;
}

ast_stmt_t *
stmt_new_loop(loop_kind_t kind, ast_stmt_t *init_stmt, ast_exp_t *cond_exp, ast_stmt_t *post_stmt,
              ast_blk_t *blk, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_LOOP, pos);

    stmt->u_loop.kind = kind;
    stmt->u_loop.init_stmt = init_stmt;
    stmt->u_loop.cond_exp = cond_exp;
    stmt->u_loop.post_stmt = post_stmt;
    stmt->u_loop.blk = blk;

    if (stmt->u_loop.blk != NULL)
        stmt->u_loop.blk->kind = BLK_LOOP;
    else
        stmt->u_loop.blk = blk_new_loop(&stmt->pos);

    return stmt;
}

ast_stmt_t *
stmt_new_switch(ast_exp_t *cond_exp, ast_blk_t *blk, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_SWITCH, pos);

    stmt->u_sw.cond_exp = cond_exp;
    stmt->u_sw.blk = blk;

    return stmt;
}

ast_stmt_t *
stmt_new_case(ast_exp_t *val_exp, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_CASE, pos);

    stmt->u_case.val_exp = val_exp;

    return stmt;
}

ast_stmt_t *
stmt_new_return(ast_exp_t *arg_exp, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_RETURN, pos);

    stmt->u_ret.arg_exp = arg_exp;

    return stmt;
}

ast_stmt_t *
stmt_new_goto(char *label, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_GOTO, pos);

    stmt->u_goto.label = label;

    return stmt;
}

ast_stmt_t *
stmt_new_jump(stmt_kind_t kind, ast_exp_t *cond_exp, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(kind, pos);

    stmt->u_jump.cond_exp = cond_exp;

    return stmt;
}

ast_stmt_t *
stmt_new_ddl(char *ddl, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_DDL, pos);

    stmt->u_ddl.ddl = ddl;

    return stmt;
}

ast_stmt_t *
stmt_new_blk(ast_blk_t *blk, src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_BLK, pos);

    stmt->u_blk.blk = blk;

    return stmt;
}

ast_stmt_t *
stmt_new_pragma(pragma_kind_t kind, ast_exp_t *val_exp, char *val_str, ast_exp_t *desc_exp,
                src_pos_t *pos)
{
    ast_stmt_t *stmt = ast_stmt_new(STMT_PRAGMA, pos);

    stmt->u_pragma.kind = kind;
    stmt->u_pragma.val_exp = val_exp;
    stmt->u_pragma.val_str = val_str;
    stmt->u_pragma.desc_exp = desc_exp;

    return stmt;
}

ast_stmt_t *
stmt_make_assign(ast_id_t *var_id, ast_exp_t *val_exp)
{
    ast_exp_t *var_exp;

    if (is_tuple_id(var_id)) {
        int i;
        vector_t *elem_exps = vector_new();
        ast_exp_t *id_exp;

        /* Since the number of elements in "val_exp" may be smaller than the number of elements
         * in "var_id", it is made as a tuple expression for asymmetry assignment processing */
        vector_foreach(var_id->u_tup.elem_ids, i) {
            ast_id_t *elem_id = vector_get_id(var_id->u_tup.elem_ids, i);

            id_exp = exp_new_id(elem_id->name, &elem_id->pos);

            id_exp->id = elem_id;
            meta_copy(&id_exp->meta, &elem_id->meta);

            vector_add_last(elem_exps, id_exp);
        }

        var_exp = exp_new_tuple(elem_exps, &val_exp->pos);
    }
    else {
        var_exp = exp_new_id(var_id->name, &var_id->pos);

        var_exp->id = var_id;
        meta_copy(&var_exp->meta, &var_id->meta);
    }

    return stmt_new_assign(var_exp, val_exp, &val_exp->pos);
}

ast_stmt_t *
stmt_make_malloc(uint32_t reg_idx, uint32_t size, uint8_t align, src_pos_t *pos)
{
    ast_exp_t *reg_exp, *call_exp;
    ast_exp_t *arg_exp;
    vector_t *arg_exps = vector_new();

    ASSERT1(align == 4 || align == 8, align);

    reg_exp = exp_new_reg(reg_idx);
    meta_set_int32(&reg_exp->meta);

    arg_exp = exp_new_lit_int(size, pos);
    meta_set_int32(&arg_exp->meta);

    exp_add(arg_exps, arg_exp);

    call_exp = exp_new_call(align == 4 ? FN_MALLOC32 : FN_MALLOC64, NULL, arg_exps, pos);
    meta_set_int32(&call_exp->meta);

    return stmt_new_assign(reg_exp, call_exp, pos);
}

/* end of ast_stmt.c */
