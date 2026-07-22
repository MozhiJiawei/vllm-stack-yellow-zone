"""GDB command for exporting the vCANN deadlock trace ABI as JSON."""

import json

import gdb


KIND_NAMES = {
    0: "INVALID", 1: "RT_KERNEL_LAUNCH", 2: "RT_KERNEL_HANDLE",
    3: "RT_KERNEL_HANDLE_V2", 4: "RT_KERNEL_FLAG", 5: "RT_KERNEL_FLAG_V2",
    6: "RT_KERNEL_EX", 7: "RT_KERNEL_FWK", 8: "RT_CPU_KERNEL",
    9: "RT_AICPU_KERNEL", 10: "RT_AICPU_KERNEL_EX", 11: "RT_FUNC_HANDLE",
    12: "RT_FUNC_HANDLE_V2", 13: "RT_FUNC_HANDLE_V3", 14: "RT_VECTOR_HANDLE",
    15: "RT_VECTOR_KERNEL", 16: "RTS_KERNEL_HOST_ARGS", 17: "RTS_CPU_KERNEL",
    18: "RTS_KERNEL_CONFIG", 19: "RTS_KERNEL_DEV_ARGS", 20: "RTS_RANDOM_TASK",
    21: "RTS_REDUCE_TASK", 22: "RTS_UPDATE_TASK", 23: "RT_FFTS_TASK",
    24: "RT_STARS_TASK", 25: "RT_CMO_TASK", 26: "RT_BARRIER_TASK",
    27: "RT_MULTIPLE_TASK", 28: "RT_MODEL_EXECUTE", 29: "RT_MODEL_EXECUTE_SYNC",
    30: "EVENT_RECORD", 31: "EVENT_WAIT", 32: "NOTIFY_RECORD",
    33: "NOTIFY_WAIT", 34: "STREAM_DESTROY", 35: "STREAM_CAPTURE_BEGIN",
    36: "STREAM_CAPTURE_END", 37: "SCHED_SYNC_BEGIN", 38: "SCHED_SYNC_END",
    39: "KERNEL_REGISTER", 40: "KERNEL_UNREGISTER",
    41: "DEVICE_SYNC_BEGIN", 42: "DEVICE_SYNC_END",
    43: "STREAM_SYNC_BEGIN", 44: "STREAM_SYNC_END",
    45: "ACL_KERNEL", 46: "ACL_KERNEL_CONFIG", 47: "ACL_KERNEL_V2",
    48: "ACL_KERNEL_HOST_ARGS",
}
STRING_OBJECT_KINDS = {7, 8, 10}
SYMBOL_OBJECT_KINDS = {1, 2, 3, 4, 5, 11, 12, 13, 14, 15, 16, 17, 18, 19, 45, 46, 47, 48}
SYMBOL_CACHE = {}
TRACE_MAGIC = 0x5643414E4E545243
TRACE_ABI_VERSION = 3
TRACE_CAPACITY = 4096
KERNEL_REGISTRY_MAGIC = 0x5643414E4E4B5247
KERNEL_REGISTRY_ABI_VERSION = 1
KERNEL_REGISTRY_CAPACITY = 512


def number(value):
    return int(value)


def pointer_text(value):
    return f"0x{value:x}" if value else "0x0"


def symbol_for(address):
    if not address:
        return ""
    if address in SYMBOL_CACHE:
        return SYMBOL_CACHE[address]
    try:
        result = gdb.execute(f"info symbol 0x{address:x}", to_string=True).strip()
        symbol = "" if result.startswith("No symbol matches") else result
    except gdb.error:
        symbol = ""
    SYMBOL_CACHE[address] = symbol
    return symbol


def string_for(address):
    if not address:
        return ""
    try:
        char_pointer = gdb.lookup_type("char").pointer()
        return gdb.Value(address).cast(char_pointer).string(length=160, errors="replace")
    except (gdb.error, UnicodeError, TypeError):
        return ""


def fixed_string(value):
    try:
        char_pointer = gdb.lookup_type("char").pointer()
        return value.address.cast(char_pointer).string(length=128, errors="replace")
    except (gdb.error, UnicodeError, TypeError):
        return ""


class VcannTraceDump(gdb.Command):
    def __init__(self):
        super().__init__("vcann-trace-dump", gdb.COMMAND_DATA)

    def invoke(self, argument, from_tty):
        del from_tty
        argv = gdb.string_to_argv(argument)
        if not argv or len(argv) > 2:
            raise gdb.GdbError(
                f"usage: vcann-trace-dump OUTPUT.json [LIMIT]; got {argument!r}"
            )
        output = argv[0]
        limit = int(argv[1]) if len(argv) == 2 else 4096
        if limit <= 0:
            raise gdb.GdbError("limit must be positive")

        try:
            trace = gdb.parse_and_eval("g_vcann_trace")
            probe = gdb.parse_and_eval("g_vcann_sync_probe")
            host_probe = gdb.parse_and_eval("g_vcann_host_sync_probe")
            registry = gdb.parse_and_eval("g_vcann_kernel_registry")
        except gdb.error as error:
            with open(output, "w", encoding="utf-8") as stream:
                json.dump({"available": False, "error": str(error)}, stream, indent=2)
                stream.write("\n")
            return

        magic = number(trace["magic"])
        abi_version = number(trace["abi_version"])
        capacity = number(trace["capacity"])
        if magic != TRACE_MAGIC or abi_version != TRACE_ABI_VERSION or capacity != TRACE_CAPACITY:
            result = {
                "available": False,
                "error": "incompatible vCANN trace ABI",
                "observed": {
                    "magic": f"0x{magic:016x}",
                    "abi_version": abi_version,
                    "capacity": capacity,
                },
                "expected": {
                    "magic": f"0x{TRACE_MAGIC:016x}",
                    "abi_version": TRACE_ABI_VERSION,
                    "capacity": TRACE_CAPACITY,
                },
            }
            with open(output, "w", encoding="utf-8") as stream:
                json.dump(result, stream, indent=2, sort_keys=True)
                stream.write("\n")
            return
        next_sequence = number(trace["next_sequence"])
        first_sequence = max(1, next_sequence - min(limit, capacity) + 1)
        sync_active = bool(number(probe["active"]))
        sync_stream = number(probe["stream"]) if sync_active else 0
        registrations = []
        registrations_by_handle = {}
        registry_magic = number(registry["magic"])
        registry_abi = number(registry["abi_version"])
        registry_capacity = number(registry["capacity"])
        if (
            registry_magic == KERNEL_REGISTRY_MAGIC
            and registry_abi == KERNEL_REGISTRY_ABI_VERSION
            and registry_capacity == KERNEL_REGISTRY_CAPACITY
        ):
            registration_count = min(number(registry["next_sequence"]), registry_capacity)
            for index in range(registration_count):
                registration = registry["entries"][index]
                sequence = number(registration["committed_sequence"])
                if sequence != index + 1:
                    continue
                handle = number(registration["handle"])
                entry = {
                    "sequence": sequence,
                    "handle": pointer_text(handle),
                    "stub": pointer_text(number(registration["stub"])),
                    "device_function": pointer_text(number(registration["device_function"])),
                    "function_mode": number(registration["function_mode"]),
                    "tid": number(registration["tid"]),
                    "stub_name": fixed_string(registration["stub_name"]),
                    "device_name": fixed_string(registration["device_name"]),
                }
                registrations.append(entry)
                registrations_by_handle.setdefault(handle, []).append(entry["stub_name"])

        records = []
        for sequence in range(first_sequence, next_sequence + 1):
            record = trace["records"][(sequence - 1) % capacity]
            if number(record["committed_sequence"]) != sequence:
                continue
            kind = number(record["kind"])
            stream_address = number(record["stream"])
            object_address = number(record["object"])
            entry = {
                "sequence": sequence,
                "timestamp_ns": number(record["timestamp_ns"]),
                "tid": number(record["tid"]),
                "kind": kind,
                "kind_name": KIND_NAMES.get(kind, f"UNKNOWN_{kind}"),
                "record_phase": (
                    "scheduler" if kind in (37, 38)
                    else "registration" if kind in (39, 40)
                    else "sync" if kind in (41, 42, 43, 44)
                    else "hook_attempt"
                ),
                "stream": pointer_text(stream_address),
                "object": pointer_text(object_address),
                "auxiliary": pointer_text(number(record["auxiliary"])),
                "value": number(record["value"]),
                "blocks": number(record["blocks"]),
                "args_size": number(record["args_size"]),
                "on_sync_stream": bool(sync_stream and stream_address == sync_stream),
            }
            if kind in SYMBOL_OBJECT_KINDS:
                symbol = symbol_for(object_address)
                if symbol:
                    entry["object_symbol"] = symbol
            if kind in STRING_OBJECT_KINDS:
                name = string_for(object_address)
                if name:
                    entry["object_name"] = name
            kernel_names = registrations_by_handle.get(object_address, [])
            if kernel_names:
                entry["kernel_names"] = sorted(set(kernel_names))
            records.append(entry)

        runtime_state = {}
        for name in ("g_vnpu_id", "g_sched_locking", "hasModelExecuteSync", "waitEventCount"):
            try:
                runtime_state[name] = number(gdb.parse_and_eval(name))
            except (gdb.error, TypeError, ValueError):
                runtime_state[name] = None
        try:
            cache = gdb.parse_and_eval("g_cache_streams")
            stream_count = min(max(number(cache["num_streams"]), 0), 128)
            runtime_state["cached_streams"] = [
                pointer_text(number(cache["streams"][index])) for index in range(stream_count)
            ]
        except (gdb.error, TypeError, ValueError):
            runtime_state["cached_streams"] = None

        result = {
            "available": True,
            "magic": f"0x{number(trace['magic']):016x}",
            "abi_version": number(trace["abi_version"]),
            "capacity": capacity,
            "enabled": bool(number(trace["enabled"])),
            "process_id": number(trace["process_id"]),
            "next_sequence": next_sequence,
            "sync_probe": {
                "active": sync_active,
                "vnpu_id": number(probe["vnpu_id"]),
                "owner": number(probe["owner"]),
                "schedule_turn": number(probe["schedule_turn"]),
                "begin_ns": number(probe["begin_ns"]),
                "begin_sequence": number(probe["begin_sequence"]),
                "stream": pointer_text(sync_stream),
            },
            "host_sync_probe": {
                "active": bool(number(host_probe["active"])),
                "kind": number(host_probe["kind"]),
                "kind_name": KIND_NAMES.get(number(host_probe["kind"]), ""),
                "tid": number(host_probe["tid"]),
                "timeout": number(host_probe["timeout"]),
                "begin_ns": number(host_probe["begin_ns"]),
                "begin_sequence": number(host_probe["begin_sequence"]),
                "stream": pointer_text(number(host_probe["stream"])),
            },
            "kernel_registry": {
                "available": (
                    registry_magic == KERNEL_REGISTRY_MAGIC
                    and registry_abi == KERNEL_REGISTRY_ABI_VERSION
                    and registry_capacity == KERNEL_REGISTRY_CAPACITY
                ),
                "magic": f"0x{registry_magic:016x}",
                "abi_version": registry_abi,
                "capacity": registry_capacity,
                "next_sequence": number(registry["next_sequence"]),
                "dropped": number(registry["dropped"]),
                "registrations": registrations,
            },
            "runtime_state": runtime_state,
            "records": records,
        }
        with open(output, "w", encoding="utf-8") as stream:
            json.dump(result, stream, indent=2, sort_keys=True)
            stream.write("\n")


VcannTraceDump()
