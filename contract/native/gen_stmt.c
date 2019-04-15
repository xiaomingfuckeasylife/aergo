/**
 * @file    gen_stmt.c
 * @copyright defined in aergo/LICENSE.txt
 */

#include "common.h"

#include "ast_id.h"
#include "ir_md.h"
#include "gen_exp.h"
#include "gen_util.h"
#include "syslib.h"

#include "gen_stmt.h"

static BinaryenExpressionRef
stmt_gen_exp(gen_t *gen, ast_stmt_t *stmt)
{
    ast_exp_t *exp = stmt->u_exp.exp;

    if (is_call_exp(exp) && !is_void_meta(&exp->meta))
        return BinaryenDrop(gen->module, exp_gen(gen, exp));

    return exp_gen(gen, exp);
}

static BinaryenExpressionRef
stmt_gen_assign(gen_t *gen, ast_stmt_t *stmt)
{
    ast_exp_t *l_exp = stmt->u_assign.l_exp;
    ast_exp_t *r_exp = stmt->u_assign.r_exp;
    ast_id_t *id = l_exp->id;
    BinaryenExpressionRef address, value;

    if (id != NULL && is_map_meta(&id->meta))
        /* TODO: If the type of identifier is map, lvalue and rvalue must be combined
         * into a call expression */
        return NULL;

    value = exp_gen(gen, r_exp);
    if (value == NULL)
        return NULL;

    if (is_global_exp(l_exp))
        return BinaryenSetGlobal(gen->module, l_exp->u_glob.name, value);

    if (is_reg_exp(l_exp))
        return BinaryenSetLocal(gen->module, l_exp->u_reg.idx, value);

    if (is_mem_exp(l_exp)) {
        address = BinaryenGetLocal(gen->module, l_exp->u_mem.base, BinaryenTypeInt32());

        if (is_array_meta(&l_exp->meta))
            return BinaryenStore(gen->module, sizeof(uint32_t),
                                 l_exp->u_mem.addr + l_exp->u_mem.offset, 0, address, value,
                                 BinaryenTypeInt32());

        return BinaryenStore(gen->module, TYPE_BYTE(l_exp->meta.type),
                             l_exp->u_mem.addr + l_exp->u_mem.offset, 0, address, value,
                             meta_gen(&l_exp->meta));
    }

    /* For an array whose index is a variable, we must dynamically determine the offset */
    ASSERT1(is_array_meta(&id->meta), id->meta.type);
    ASSERT(!is_array_meta(&l_exp->meta));

    gen->is_lval = true;
    address = exp_gen(gen, l_exp);
    gen->is_lval = false;

    return BinaryenStore(gen->module, TYPE_BYTE(l_exp->meta.type), 0, 0, address, value,
                         meta_gen(&l_exp->meta));
}

static BinaryenExpressionRef
stmt_gen_ddl(gen_t *gen, ast_stmt_t *stmt)
{
    /* TODO */
    return NULL;
}

static BinaryenExpressionRef
stmt_gen_pragma(gen_t *gen, ast_stmt_t *stmt)
{
    ir_md_t *md = gen->md;
    BinaryenExpressionRef condition, description;

    condition = exp_gen(gen, stmt->u_pragma.val_exp);

    if (stmt->u_pragma.desc_exp != NULL)
        description = exp_gen(gen, stmt->u_pragma.desc_exp);
    else
        description = i32_gen(gen, 0);

    return syslib_gen(gen, FN_ASSERT, 3, condition,
                      i32_gen(gen, sgmt_add_str(&md->sgmt, stmt->u_pragma.val_str)), description);
}

BinaryenExpressionRef
stmt_gen(gen_t *gen, ast_stmt_t *stmt)
{
    switch (stmt->kind) {
    case STMT_EXP:
        return stmt_gen_exp(gen, stmt);

    case STMT_ASSIGN:
        return stmt_gen_assign(gen, stmt);

    case STMT_RETURN:
        return BinaryenReturn(gen->module, exp_gen(gen, stmt->u_ret.arg_exp));

    case STMT_DDL:
        return stmt_gen_ddl(gen, stmt);

    case STMT_PRAGMA:
        return stmt_gen_pragma(gen, stmt);

    default:
        ASSERT1(!"invalid statement", stmt->kind);
    }

    return NULL;
}

/* end of gen_stmt.c */
