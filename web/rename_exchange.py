#!/usr/bin/env python3
"""Atomically exchange two existing Linux filesystem paths with renameat2."""

import ctypes
import os
import sys


def main() -> int:
	if len(sys.argv) != 3:
		raise SystemExit("usage: rename_exchange.py <left> <right>")
	left = os.fsencode(os.path.abspath(sys.argv[1]))
	right = os.fsencode(os.path.abspath(sys.argv[2]))
	libc = ctypes.CDLL(None, use_errno=True)
	renameat2 = libc.renameat2
	renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
	renameat2.restype = ctypes.c_int
	at_fdcwd = -100
	rename_exchange = 2
	if renameat2(at_fdcwd, left, at_fdcwd, right, rename_exchange) != 0:
		error = ctypes.get_errno()
		raise OSError(error, os.strerror(error), os.fsdecode(left), os.fsdecode(right))
	for directory in {os.path.dirname(left), os.path.dirname(right)}:
		handle = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
		try:
			os.fsync(handle)
		finally:
			os.close(handle)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
