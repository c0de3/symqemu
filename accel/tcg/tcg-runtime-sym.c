#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "qemu/qemu-print.h"
#include "tcg.h"

/* Include the symbolic backend, using void* as expression type. */

#define SymExpr void*
#include "RuntimeCommon.h"

/* Returning NULL for unimplemented functions is equivalent to concretizing and
 * allows us to run without all symbolic handlers fully implemented. */

#define NOT_IMPLEMENTED NULL

/* A slightly questionable macro to help with the repetitive parts of
 * implementing the symbolic handlers: assuming the existence of concrete
 * arguments "arg1" and "arg2" along with variables "arg1_expr" and "arg2_expr"
 * for the corresponding expressions, it expands into code that returns early if
 * both expressions are NULL and otherwise creates the missing expression.*/

#define BINARY_HELPER_ENSURE_EXPRESSIONS                                       \
    if (arg1_expr == NULL && arg2_expr == NULL) {                              \
        return NULL;                                                           \
    }                                                                          \
                                                                               \
    if (arg1_expr == NULL) {                                                   \
        arg1_expr = _sym_build_integer(arg1, _sym_bits_helper(arg2_expr));     \
    }                                                                          \
                                                                               \
    if (arg2_expr == NULL) {                                                   \
        arg2_expr = _sym_build_integer(arg2, _sym_bits_helper(arg1_expr));     \
    }

/* This macro declares a binary helper function with 64-bit arguments and
 * defines a 32-bit helper function that delegates to it. Use it instead of the
 * function prototype in helper definitions. */

#define DECL_HELPER_BINARY(name)                                               \
    void *HELPER(sym_##name##_i32)(uint32_t arg1, void *arg1_expr,             \
                                   uint32_t arg2, void *arg2_expr) {           \
        return HELPER(sym_##name##_i64)(arg1, arg1_expr, arg2, arg2_expr);     \
    }                                                                          \
                                                                               \
    void *HELPER(sym_##name##_i64)(uint64_t arg1, void *arg1_expr,             \
                                   uint64_t arg2, void *arg2_expr)

/* To save implementation effort, the macro below defines handlers following the
 * standard scheme of binary operations:
 *
 * 1. Return NULL if both operands are concrete.
 * 2. Create any missing expression.
 * 3. Create an expression representing the operation.
 *
 * For example, DEF_HELPER_BINARY(divu, unsigned_div) defines helpers
 * "helper_sym_divu_i32/i64" backed by the run-time function
 * "_sym_build_unsigned_div". The 32-bit helper just extends the arguments and
 * calls the 64-bit helper. */

#define DEF_HELPER_BINARY(qemu_name, symcc_name)                               \
    DECL_HELPER_BINARY(qemu_name) {                                            \
        BINARY_HELPER_ENSURE_EXPRESSIONS;                                      \
        return _sym_build_##symcc_name(arg1_expr, arg2_expr);                  \
    }

/* The binary helpers */

DEF_HELPER_BINARY(add, add)
DEF_HELPER_BINARY(sub, sub)
DEF_HELPER_BINARY(mul, mul)
DEF_HELPER_BINARY(div, signed_div)
DEF_HELPER_BINARY(divu, unsigned_div)
DEF_HELPER_BINARY(rem, signed_rem)
DEF_HELPER_BINARY(remu, unsigned_rem)
DEF_HELPER_BINARY(and, and)
DEF_HELPER_BINARY(or, or)
DEF_HELPER_BINARY(xor, xor)
DEF_HELPER_BINARY(shift_right, logical_shift_right)
DEF_HELPER_BINARY(arithmetic_shift_right, arithmetic_shift_right)
DEF_HELPER_BINARY(shift_left, shift_left)

void *HELPER(sym_neg)(void *expr)
{
    if (expr == NULL)
        return NULL;

    return _sym_build_sub(
        _sym_build_integer(0, _sym_bits_helper(expr)), expr);
}

DECL_HELPER_BINARY(andc)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;
    return _sym_build_and(arg1_expr, _sym_build_neg(arg2_expr));
}

DECL_HELPER_BINARY(eqv)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;
    return _sym_build_neg(_sym_build_xor(arg1_expr, arg2_expr));
}

DECL_HELPER_BINARY(nand)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;
    return _sym_build_neg(_sym_build_and(arg1_expr, arg2_expr));
}

DECL_HELPER_BINARY(nor)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;
    return _sym_build_neg(_sym_build_or(arg1_expr, arg2_expr));
}

DECL_HELPER_BINARY(orc)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;
    return _sym_build_or(arg1_expr, _sym_build_neg(arg2_expr));
}

void *HELPER(sym_not)(void *expr)
{
    if (expr == NULL)
        return NULL;

    return _sym_build_neg(expr);
}

void *HELPER(sym_sext_or_trunc)(void *expr, uint64_t target_length)
{
    if (expr == NULL)
        return NULL;

    size_t current_bits = _sym_bits_helper(expr);
    size_t desired_bits = target_length * 8;

    if (current_bits == desired_bits)
        return expr;
    if (current_bits > desired_bits)
        return _sym_build_trunc(expr, desired_bits);
    if (current_bits < desired_bits)
        return _sym_build_sext(expr, desired_bits);

    g_assert_not_reached();
}

void *HELPER(sym_zext_or_trunc)(void *expr, uint64_t target_length)
{
    if (expr == NULL)
        return NULL;

    size_t current_bits = _sym_bits_helper(expr);
    size_t desired_bits = target_length * 8;

    if (current_bits == desired_bits)
        return expr;
    if (current_bits > desired_bits)
        return _sym_build_trunc(expr, desired_bits);
    if (current_bits < desired_bits)
        return _sym_build_zext(expr, desired_bits);

    g_assert_not_reached();
}

void *HELPER(sym_bswap)(void *expr, uint64_t length)
{
    /* TODO */
    return NOT_IMPLEMENTED;
}

void *HELPER(sym_load_guest)(target_ulong addr, void *addr_expr, uint64_t length)
{
    /* TODO try an alternative address; cast the address to uint64_t */
    return _sym_read_memory((uint8_t*)addr, length, true);
}

void HELPER(sym_store_guest_i32)(uint32_t value, void *value_expr,
                                 target_ulong addr, void *addr_expr,
                                 uint64_t length)
{
    /* TODO try alternative address */

    _sym_write_memory((uint8_t*)addr, length, value_expr, true);
}

void HELPER(sym_store_guest_i64)(uint64_t value, void *value_expr,
                                 target_ulong addr, void *addr_expr,
                                 uint64_t length)
{
    /* TODO try alternative address */

    _sym_write_memory((uint8_t*)addr, length, value_expr, true);
}

void *HELPER(sym_load_host)(void *addr, uint64_t offset, uint64_t length)
{
    return _sym_read_memory((uint8_t*)addr + offset, length, true);
}

void HELPER(sym_store_host_i32)(uint32_t value, void *value_expr,
                                void *addr,
                                uint64_t offset, uint64_t length)
{
    _sym_write_memory((uint8_t*)addr + offset, length, value_expr, true);
}

void HELPER(sym_store_host_i64)(uint64_t value, void *value_expr,
                                void *addr,
                                uint64_t offset, uint64_t length)
{
    _sym_write_memory((uint8_t*)addr + offset, length, value_expr, true);
}

DECL_HELPER_BINARY(rotate_left)
{
    /* TODO */
    return NOT_IMPLEMENTED;
}

DECL_HELPER_BINARY(rotate_right)
{
    /* TODO */
    return NOT_IMPLEMENTED;
}

void *HELPER(sym_extract_i32)(void *expr, uint32_t ofs, uint32_t len)
{
    return HELPER(sym_extract_i64)(expr, ofs, len);
}

void *HELPER(sym_extract_i64)(void *expr, uint64_t ofs, uint64_t len)
{
    if (expr == NULL)
        return NULL;

    return _sym_build_zext(
        _sym_extract_helper(expr, ofs + len - 1, ofs),
        _sym_bits_helper(expr));
}

void *HELPER(sym_extract2_i32)(uint32_t ah, void *ah_expr,
                               uint32_t al, void *al_expr,
                               uint64_t ofs)
{
    /* TODO */
    return NOT_IMPLEMENTED;
}

void *HELPER(sym_extract2_i64)(uint64_t ah, void *ah_expr,
                               uint64_t al, void *al_expr,
                               uint64_t ofs)
{
    /* TODO */
    return NOT_IMPLEMENTED;
}

void *HELPER(sym_sextract_i32)(void *expr, uint32_t ofs, uint32_t len)
{
    return HELPER(sym_sextract_i64)(expr, ofs, len);
}

void *HELPER(sym_sextract_i64)(void *expr, uint64_t ofs, uint64_t len)
{
    if (expr == NULL)
        return NULL;

    return _sym_build_sext(
        _sym_extract_helper(expr, ofs + len - 1, ofs),
        _sym_bits_helper(expr));
}

void *HELPER(sym_deposit_i32)(uint32_t arg1, void *arg1_expr,
                              uint32_t arg2, void *arg2_expr,
                              uint32_t ofs, uint32_t len)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;

    /* The symbolic implementation follows the alternative concrete
     * implementation of tcg_gen_deposit_i32 in tcg-op.c (which handles
     * architectures that don't support deposit directly). */

    uint32_t mask = (1u << len) - 1;
    return _sym_build_or(
        _sym_build_and(
            arg1_expr,
            _sym_build_integer(~(mask << ofs), 32)),
        _sym_build_shift_left(
            _sym_build_and(arg2_expr, _sym_build_integer(mask, 32)),
            _sym_build_integer(ofs, 32)));
}

void *HELPER(sym_deposit_i64)(uint64_t arg1, void *arg1_expr,
                              uint64_t arg2, void *arg2_expr,
                              uint64_t ofs, uint64_t len)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS;

    /* The symbolic implementation follows the alternative concrete
     * implementation of tcg_gen_deposit_i64 in tcg-op.c (which handles
     * architectures that don't support deposit directly). */

    uint64_t mask = (1ull << len) - 1;
    return _sym_build_or(
        _sym_build_and(
            arg1_expr,
            _sym_build_integer(~(mask << ofs), 64)),
        _sym_build_shift_left(
            _sym_build_and(arg2_expr, _sym_build_integer(mask, 64)),
            _sym_build_integer(ofs, 64)));
}

static void *sym_setcond_internal(uint64_t arg1, void *arg1_expr,
                                  uint64_t arg2, void *arg2_expr,
                                  int32_t cond, uint64_t result,
                                  uint8_t result_bits)
{
    BINARY_HELPER_ENSURE_EXPRESSIONS

    void *(*handler)(void *, void*);
    switch (cond) {
    case TCG_COND_EQ:
        handler = _sym_build_equal;
        break;
    case TCG_COND_NE:
        handler = _sym_build_not_equal;
        break;
    case TCG_COND_LT:
        handler = _sym_build_signed_less_than;
        break;
    case TCG_COND_GE:
        handler = _sym_build_signed_greater_equal;
        break;
    case TCG_COND_LE:
        handler = _sym_build_signed_less_equal;
        break;
    case TCG_COND_GT:
        handler = _sym_build_signed_greater_than;
        break;
    case TCG_COND_LTU:
        handler = _sym_build_unsigned_less_than;
        break;
    case TCG_COND_GEU:
        handler = _sym_build_unsigned_greater_equal;
        break;
    case TCG_COND_LEU:
        handler = _sym_build_unsigned_less_equal;
        break;
    case TCG_COND_GTU:
        handler = _sym_build_unsigned_greater_than;
        break;
    default:
        g_assert_not_reached();
    }

    void *condition = handler(arg1_expr, arg2_expr);
    /* TODO */
    _sym_push_path_constraint(condition, result, 42);

    return _sym_build_bool_to_bits(condition, result_bits);
}

void *HELPER(sym_setcond_i32)(uint32_t arg1, void *arg1_expr,
                              uint32_t arg2, void *arg2_expr,
                              int32_t cond, uint32_t result)
{
    return sym_setcond_internal(
        arg1, arg1_expr, arg2, arg2_expr, cond, result, 32);
}

void *HELPER(sym_setcond_i64)(uint64_t arg1, void *arg1_expr,
                              uint64_t arg2, void *arg2_expr,
                              int32_t cond, uint64_t result)
{
    return sym_setcond_internal(
        arg1, arg1_expr, arg2, arg2_expr, cond, result, 64);
}