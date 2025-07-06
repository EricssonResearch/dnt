# To access the helper functions, source this file from gdb or from .gdbinit
# (gdb) source doc/r2dtwo_gdb_helpers.py
# Check user-defined helpers: (gdb) help user-defined

import gdb

class ConfActionForeach(gdb.Command):
    """
    Walk through a ConfAction list.
    Usage example:
        (gdb) ca_foreach stst->actions

    Alternative:
        (gdb) call log_set_level ("CONFIG", DEBUG)
        (gdb) call confactions_log (stst->actions, 3)
    """

    def __init__(self) -> None:
        super().__init__("ca_foreach", gdb.COMMAND_USER)

    def _ca_foreach(self, val):
        ret = f"ConfAction list (head: {val}):"
        ca = val
        while ca:
            ret += f"\n({ca}): {ca['text'].string()}"
            ca = ca["next"]
        return ret

    def complete(self, text: str, word: str) -> object:
        return gdb.COMPLETE_SYMBOL

    def invoke(self, args, from_tty):
        node = gdb.parse_and_eval(args)
        if str(node.type) != "struct ConfAction *":
            print("Expected pointer argument type: ConfAction *")
            return
        print(self._ca_foreach(node))

ConfActionForeach()

# TODO: foreach HashMap helper
