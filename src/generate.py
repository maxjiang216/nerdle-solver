"""
Generate all valid N-character Nerdle equations and save to equations_N.txt.

By default excludes standalone 0 on LHS (official Nerdle rules).
Use the C++ version (./generate) for much faster generation.

    python generate.py              # classic 8-char (no standalone 0)
    python generate.py --len 5       # micro 5-char
    python generate.py --allow-standalone-zero   # include 0+1=1, etc.
"""

import argparse
import os
from itertools import product as iproduct

OPERATORS = ('+', '-', '*', '/')


def valid_numbers(length: int, allow_zero: bool = True):
    """Yield valid numeric tokens. allow_zero=False excludes standalone 0 on LHS."""
    if length == 1:
        start = 0 if allow_zero else 1
        for d in range(start, 10):
            yield str(d)
    elif length >= 2:
        for first in range(1, 10):
            prefix = str(first)
            for rest in iproduct('0123456789', repeat=length - 1):
                yield prefix + ''.join(rest)


def valid_expressions(length: int, no_standalone_zero: bool = False,
                      require_op: bool = True):
    """Yield valid LHS expressions.
    no_standalone_zero: exclude 0+1=1, 1+0=1, etc.
    require_op: if True, LHS must contain at least one operator (no bare numbers).
    """
    nums = lambda l: valid_numbers(l, allow_zero=not no_standalone_zero)
    if not require_op:
        yield from nums(length)

    for num_len in range(1, length - 1):
        sub_len = length - num_len - 1
        if sub_len < 1:
            continue
        for num in nums(num_len):
            for op in OPERATORS:
                for sub in valid_expressions(sub_len, no_standalone_zero, require_op=False):
                    yield num + op + sub


def safe_eval(expr: str):
    """
    Evaluate an arithmetic expression string.
    Returns a non-negative integer if the result is exact, else None.
    Uses integer-division check: replace / with // and compare to float result.
    """
    try:
        # Float evaluation (Python uses true division)
        float_val = eval(expr)  # noqa: S307
        if not isinstance(float_val, (int, float)):
            return None
        if float_val != float_val:  # NaN check
            return None
        int_val = int(float_val)
        if int_val != float_val:  # Non-integer result (e.g. 5/2)
            return None
        if int_val < 0:
            return None
        return int_val
    except ZeroDivisionError:
        return None
    except Exception:
        return None


def generate_equations(eq_len: int = 8, exclude_zero_result: bool = False,
                       no_standalone_zero: bool = False,
                       require_op: bool = True) -> list[str]:
    """
    Generate all valid Nerdle equations of length `eq_len`.
    Returns a sorted list.

    exclude_zero_result: if True, skip equations where the result is 0
    no_standalone_zero: if True, exclude standalone 0 on LHS (0+1=1, 1+0=1, etc.)
    require_op: if True, LHS must have at least one operator (no bare numbers).
    """
    equations = set()

    for eq_pos in range(1, eq_len - 1):
        lhs_len = eq_pos
        rhs_len = eq_len - eq_pos - 1

        for lhs in valid_expressions(lhs_len, no_standalone_zero, require_op):
            val = safe_eval(lhs)
            if val is None:
                continue
            if exclude_zero_result and val == 0:
                continue
            rhs = str(val)
            if len(rhs) == rhs_len:
                equations.add(lhs + '=' + rhs)

    return sorted(equations)


def equations_file(eq_len: int) -> str:
    base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, 'data', f'equations_{eq_len}.txt')


def main():
    parser = argparse.ArgumentParser(description='Generate Nerdle equations')
    parser.add_argument('--len', type=int, default=8, dest='eq_len',
                        help='Equation length 5-8 (default: 8)')
    parser.add_argument('--no-zero', action='store_true',
                        help='Exclude equations where the result is 0 (e.g. X*0=0)')
    parser.add_argument('--allow-standalone-zero', action='store_true',
                        help='Allow standalone 0 on LHS (0+1=1, 1+0=1, etc.). Default: excluded (official rules)')
    parser.add_argument('--allow-bare', action='store_true',
                        help='Allow bare LHS (e.g. 18=18). Default: LHS must have ≥1 operator')
    args = parser.parse_args()

    if args.eq_len not in (5, 6, 7, 8):
        parser.error("Length must be 5, 6, 7, or 8.")
    no_standalone_zero = not args.allow_standalone_zero
    require_op = not args.allow_bare
    print(f"Generating all valid {args.eq_len}-character Nerdle equations...")
    eqs = generate_equations(args.eq_len, exclude_zero_result=args.no_zero,
                            no_standalone_zero=no_standalone_zero,
                            require_op=require_op)
    print(f"Found {len(eqs):,} valid equations.")

    out = equations_file(args.eq_len)
    with open(out, 'w') as f:
        f.write('\n'.join(eqs))
    print(f"Saved to {out}")


if __name__ == '__main__':
    main()
