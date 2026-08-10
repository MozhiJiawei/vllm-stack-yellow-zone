#!/usr/bin/env python3
"""Configuration and single-case runtime for the fixed AllReduce + X probe."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import importlib.metadata
import importlib.util
import multiprocessing as mp
import os
from pathlib import Path
import queue
import random
import socket
import sys
import time
import traceback
from typing import Any, Callable, Mapping

import yaml


TP = 8
ALLREDUCE_SHAPE = (4, 5120)
ALLREDUCE_DTYPE = "bfloat16"
KINDS = {"vector", "cube", "fused"}
DTYPES = {"float16", "bfloat16", "float32", "int8"}


class ConfigError(ValueError):
    """The YAML configuration is invalid."""


class UnsupportedOperator(RuntimeError):
    """The installed runtime does not expose the requested operator."""


@dataclass(frozen=True)
class OperatorSpec:
    kind: str
    expected_core: str
    dtypes: tuple[str, ...]
    shape_keys: tuple[str, ...]
    param_keys: tuple[str, ...] = ()
    cube_aligned: tuple[str, ...] = ()
    memory_factor: float = 3.0


@dataclass(frozen=True)
class Scenario:
    id: str
    operator: str
    kind: str
    expected_core: str
    dtype: str
    shape: Mapping[str, Any]
    params: Mapping[str, Any]
    source: str


@dataclass(frozen=True)
class Settings:
    repeat: int
    startup_timeout: float
    operator_timeout: float
    hold_confirm_seconds: float
    case_timeout: float
    memory_limit_mib: float


REGISTRY: dict[str, OperatorSpec] = {
    "add": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols")),
    "mul": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols")),
    "exp": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "sigmoid": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "relu": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "silu": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "gelu": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "cast": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), ("target_dtype",), memory_factor=2),
    "reduce_sum": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "softmax": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=2),
    "sort": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), memory_factor=3),
    "topk": OperatorSpec("vector", "vector", ("float16", "bfloat16", "float32"), ("rows", "cols"), ("k",), memory_factor=3),
    "matmul": OperatorSpec("cube", "cube", ("float16", "bfloat16", "float32"), ("m", "k", "n"), cube_aligned=("m", "k", "n"), memory_factor=1),
    "batch_matmul": OperatorSpec("cube", "cube", ("float16", "bfloat16", "float32"), ("batch", "m", "k", "n"), cube_aligned=("m", "k", "n"), memory_factor=1),
    "addmm": OperatorSpec("cube", "cube", ("float16", "bfloat16", "float32"), ("m", "k", "n"), cube_aligned=("m", "k", "n"), memory_factor=1),
    "conv2d": OperatorSpec("cube", "cube", ("float16", "bfloat16", "float32"), ("batch", "in_channels", "out_channels", "height", "width", "kernel"), cube_aligned=("in_channels", "out_channels"), memory_factor=2),
    "conv3d": OperatorSpec("cube", "cube", ("float16", "bfloat16", "float32"), ("batch", "in_channels", "out_channels", "depth", "height", "width", "kernel"), cube_aligned=("in_channels", "out_channels"), memory_factor=2),
    "quant_batch_matmul": OperatorSpec("cube", "cube", ("int8",), ("m", "k", "n"), cube_aligned=("m", "k", "n"), memory_factor=1),
    "layer_norm": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("rows", "hidden"), memory_factor=4),
    "rms_norm": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("rows", "hidden"), memory_factor=4),
    "add_rms_norm": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("rows", "hidden"), memory_factor=5),
    "swiglu": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("rows", "hidden"), memory_factor=3),
    "rotary_mul": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("batch", "seq", "heads", "head_dim"), memory_factor=4),
    "fusion_attention": OperatorSpec("fused", "mixed", ("float16", "bfloat16"), ("batch", "seq", "heads", "head_dim"), memory_factor=5),
    "prompt_flash_attention": OperatorSpec("fused", "mixed", ("float16", "bfloat16"), ("batch", "seq", "heads", "head_dim"), memory_factor=5),
    "incre_flash_attention": OperatorSpec("fused", "mixed", ("float16", "bfloat16"), ("batch", "kv_seq", "heads", "kv_heads", "head_dim"), memory_factor=4),
    "ffn": OperatorSpec("fused", "mixed", ("float16", "bfloat16"), ("tokens", "hidden", "intermediate"), ("activation",), memory_factor=1),
    "grouped_matmul": OperatorSpec("fused", "cube", ("float16", "bfloat16"), ("groups", "m", "k", "n"), cube_aligned=("m", "k", "n"), memory_factor=1),
    "npu_top_k_top_p": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("batch", "vocab"), ("top_k", "top_p"), memory_factor=3),
    "custom_top_k_top_p": OperatorSpec("fused", "vector", ("float16", "bfloat16", "float32"), ("batch", "vocab"), ("top_k", "top_p"), memory_factor=3),
}


def _positive_int(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ConfigError(f"{label} must be a positive integer")
    return value


def _dtype_bytes(dtype: str) -> int:
    return {"int8": 1, "float16": 2, "bfloat16": 2, "float32": 4}[dtype]


def _shape_elements(operator: str, shape: Mapping[str, Any], spec: OperatorSpec) -> int:
    values = {key: int(shape[key]) for key in spec.shape_keys}
    if operator in {"matmul", "addmm", "quant_batch_matmul"}:
        return values["m"] * values["k"] + values["k"] * values["n"] + values["m"] * values["n"]
    if operator == "batch_matmul":
        return values["batch"] * (values["m"] * values["k"] + values["k"] * values["n"] + values["m"] * values["n"])
    if operator == "conv2d":
        b, ci, co, h, w, kernel = (values[key] for key in ("batch", "in_channels", "out_channels", "height", "width", "kernel"))
        return b * (ci + co) * h * w + co * ci * kernel * kernel
    if operator == "conv3d":
        b, ci, co, d, h, w, kernel = (values[key] for key in ("batch", "in_channels", "out_channels", "depth", "height", "width", "kernel"))
        return b * (ci + co) * d * h * w + co * ci * kernel**3
    if operator in {"fusion_attention", "prompt_flash_attention", "rotary_mul"}:
        return int(spec.memory_factor * values["batch"] * values["seq"] * values["heads"] * values["head_dim"])
    if operator == "incre_flash_attention":
        return int(spec.memory_factor * values["batch"] * values["kv_seq"] * values["kv_heads"] * values["head_dim"])
    if operator == "ffn":
        tokens, hidden, intermediate = values["tokens"], values["hidden"], values["intermediate"]
        return tokens * hidden + hidden * intermediate + intermediate * hidden + tokens * intermediate
    if operator == "grouped_matmul":
        return values["groups"] * (values["m"] * values["k"] + values["k"] * values["n"] + values["m"] * values["n"])
    total = 1
    for value in values.values():
        total *= value
    return int(total * spec.memory_factor)


def _validate_shape(operator: str, shape: Mapping[str, Any], spec: OperatorSpec) -> None:
    if not isinstance(shape, Mapping):
        raise ConfigError(f"{operator}: shape entry must be a mapping")
    if shape.get("profile") != "full_core":
        raise ConfigError(f"{operator}: first version only supports profile=full_core")
    missing = set(spec.shape_keys) - set(shape)
    extra = set(shape) - {"profile", *spec.shape_keys}
    if missing or extra:
        raise ConfigError(f"{operator}: invalid shape keys; missing={sorted(missing)} extra={sorted(extra)}")
    for key in spec.shape_keys:
        _positive_int(shape[key], f"{operator}.shape.{key}")
    for key in spec.cube_aligned:
        if int(shape[key]) % 16:
            raise ConfigError(f"{operator}.shape.{key} must be divisible by 16")
    if spec.expected_core == "vector":
        partitions = max(int(shape.get(key, 0)) for key in ("rows", "batch", "seq"))
        if int(partitions) < 40:
            raise ConfigError(f"{operator}: full_core requires at least 40 vector work partitions")
    if spec.expected_core == "cube" and {"m", "n"} <= set(shape):
        tiles = int(shape["m"]) // 16 * (int(shape["n"]) // 16)
        if tiles < 20:
            raise ConfigError(f"{operator}: full_core requires at least 20 cube tiles")


def _validate_params(operator: str, params: Mapping[str, Any], shape: Mapping[str, Any]) -> None:
    if operator == "cast" and params["target_dtype"] not in DTYPES:
        raise ConfigError(f"cast.target_dtype must be one of {sorted(DTYPES)}")
    if operator == "topk" and not 1 <= _positive_int(params["k"], "topk.params.k") <= int(shape["cols"]):
        raise ConfigError("topk.params.k must not exceed shape.cols")
    if operator in {"npu_top_k_top_p", "custom_top_k_top_p"}:
        if not 1 <= _positive_int(params["top_k"], f"{operator}.params.top_k") <= int(shape["vocab"]):
            raise ConfigError(f"{operator}.params.top_k must not exceed shape.vocab")
        top_p = params["top_p"]
        if isinstance(top_p, bool) or not isinstance(top_p, (int, float)) or not 0 < float(top_p) <= 1:
            raise ConfigError(f"{operator}.params.top_p must be in (0, 1]")
    if operator == "ffn" and params["activation"] not in {"gelu", "fastgelu", "relu", "silu"}:
        raise ConfigError("ffn.params.activation is unsupported")


def load_config(path: Path) -> tuple[list[Scenario], Settings, dict[str, Any]]:
    try:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ConfigError(f"cannot load config {path}: {error}") from error
    if not isinstance(raw, Mapping) or raw.get("version") != 1:
        raise ConfigError("config version must be 1")
    baseline = raw.get("baseline")
    if not isinstance(baseline, Mapping):
        raise ConfigError("baseline must be a mapping")
    if baseline.get("tp") != TP or baseline.get("submit_ranks") != [0]:
        raise ConfigError("baseline must use TP8 with submit_ranks=[0]")
    if baseline.get("dtype") != ALLREDUCE_DTYPE or baseline.get("shape") != list(ALLREDUCE_SHAPE):
        raise ConfigError("baseline must use bfloat16 shape [4, 5120]")

    defaults = raw.get("defaults", {})
    if not isinstance(defaults, Mapping):
        raise ConfigError("defaults must be a mapping")
    settings = Settings(
        repeat=_positive_int(defaults.get("repeat", 1), "defaults.repeat"),
        startup_timeout=float(defaults.get("startup_timeout", 600)),
        operator_timeout=float(defaults.get("operator_timeout", 30)),
        hold_confirm_seconds=float(defaults.get("hold_confirm_seconds", 3)),
        case_timeout=float(defaults.get("case_timeout", 900)),
        memory_limit_mib=float(defaults.get("memory_limit_mib", 1024)),
    )
    if min(settings.startup_timeout, settings.operator_timeout, settings.hold_confirm_seconds, settings.case_timeout, settings.memory_limit_mib) <= 0:
        raise ConfigError("timeouts and memory_limit_mib must be positive")

    entries = raw.get("scenarios")
    if not isinstance(entries, list) or not entries:
        raise ConfigError("scenarios must be a non-empty list")
    scenarios: list[Scenario] = []
    seen: set[str] = set()
    for index, entry in enumerate(entries):
        if not isinstance(entry, Mapping):
            raise ConfigError(f"scenarios[{index}] must be a mapping")
        scenario_id = entry.get("id")
        if not isinstance(scenario_id, str) or not scenario_id:
            raise ConfigError(f"scenarios[{index}].id must be a non-empty string")
        if scenario_id in seen:
            raise ConfigError(f"duplicate scenario id: {scenario_id}")
        seen.add(scenario_id)
        operator = entry.get("operator")
        if operator not in REGISTRY:
            raise ConfigError(f"{scenario_id}: unknown operator {operator!r}")
        spec = REGISTRY[str(operator)]
        kind = entry.get("kind")
        expected_core = entry.get("expected_core")
        dtype = entry.get("dtype")
        if kind != spec.kind or kind not in KINDS:
            raise ConfigError(f"{scenario_id}: kind must be {spec.kind}")
        if expected_core != spec.expected_core:
            raise ConfigError(f"{scenario_id}: expected_core must be {spec.expected_core}")
        if dtype not in spec.dtypes:
            raise ConfigError(f"{scenario_id}: dtype {dtype!r} not supported; expected one of {spec.dtypes}")
        params = entry.get("params", {})
        if not isinstance(params, Mapping):
            raise ConfigError(f"{scenario_id}: params must be a mapping")
        unknown_params = set(params) - set(spec.param_keys)
        missing_params = set(spec.param_keys) - set(params)
        if unknown_params or missing_params:
            raise ConfigError(f"{scenario_id}: invalid params; missing={sorted(missing_params)} extra={sorted(unknown_params)}")
        shapes = entry.get("shapes")
        if not isinstance(shapes, list) or not shapes:
            raise ConfigError(f"{scenario_id}: shapes must be a non-empty list")
        source = entry.get("source")
        if not isinstance(source, str) or not source:
            raise ConfigError(f"{scenario_id}: source must be a non-empty string")
        for shape_index, shape in enumerate(shapes):
            _validate_shape(str(operator), shape, spec)
            _validate_params(str(operator), params, shape)
            estimated_mib = _shape_elements(str(operator), shape, spec) * _dtype_bytes(str(dtype)) / 1024**2
            if estimated_mib > settings.memory_limit_mib:
                raise ConfigError(f"{scenario_id}.shapes[{shape_index}] estimates {estimated_mib:.1f} MiB, above limit {settings.memory_limit_mib:.1f} MiB")
            scenarios.append(Scenario(scenario_id, str(operator), str(kind), str(expected_core), str(dtype), dict(shape), dict(params), source))
    return scenarios, settings, dict(baseline)


def package_version(name: str) -> str:
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return "unknown"


def environment_fingerprint() -> dict[str, Any]:
    return {
        "python": sys.version.split()[0],
        "torch": package_version("torch"),
        "torch_npu": package_version("torch-npu"),
        "vllm_ascend": package_version("vllm-ascend"),
        "xlite": package_version("xlite"),
        "cann": "8.5.1",
        "soc": "Ascend910B4",
    }


def free_xlite_port() -> int:
    for _ in range(1000):
        port = random.randint(20000, 39000)
        sockets: list[socket.socket] = []
        try:
            for candidate in (port, port + 400):
                sock = socket.socket()
                sock.bind(("127.0.0.1", candidate))
                sockets.append(sock)
            return port
        except OSError:
            pass
        finally:
            for sock in sockets:
                sock.close()
    raise RuntimeError("cannot find a free XLITE_PORT pair")


def _require_npu_op(torch: Any, name: str) -> Callable[..., Any]:
    namespace = getattr(torch.ops, "npu", None)
    operation = getattr(namespace, name, None) if namespace is not None else None
    if operation is None:
        raise UnsupportedOperator(f"torch.ops.npu.{name} is unavailable")
    return operation


def _register_custom_opp() -> None:
    spec = importlib.util.find_spec("vllm_ascend")
    if spec is None or not spec.submodule_search_locations:
        raise UnsupportedOperator("vllm_ascend is unavailable")
    package_root = Path(next(iter(spec.submodule_search_locations)))
    custom_opp = package_root / "_cann_ops_custom" / "vendors" / "vllm-ascend"
    if not custom_opp.is_dir():
        raise UnsupportedOperator(f"custom OPP directory is missing: {custom_opp}")
    entries = [item for item in os.environ.get("ASCEND_CUSTOM_OPP_PATH", "").split(":") if item]
    if str(custom_opp) not in entries:
        os.environ["ASCEND_CUSTOM_OPP_PATH"] = ":".join([str(custom_opp), *entries])


def build_operation(torch: Any, torch_npu: Any, scenario: Scenario, device: str) -> Callable[[], Any]:
    shape, params, op = scenario.shape, scenario.params, scenario.operator
    dtype = getattr(torch, scenario.dtype)

    if op in {"add", "mul"}:
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        y = torch.randn_like(x)
        return (lambda: x + y) if op == "add" else (lambda: x * y)
    if op in {"exp", "sigmoid", "relu", "silu", "gelu"}:
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        functions = {
            "exp": torch.exp,
            "sigmoid": torch.sigmoid,
            "relu": torch.relu,
            "silu": torch.nn.functional.silu,
            "gelu": torch.nn.functional.gelu,
        }
        return lambda: functions[op](x)
    if op == "cast":
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        target = getattr(torch, str(params["target_dtype"]))
        return lambda: x.to(target)
    if op == "reduce_sum":
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        return lambda: torch.sum(x, dim=-1)
    if op == "softmax":
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        return lambda: torch.softmax(x, dim=-1)
    if op == "sort":
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        return lambda: torch.sort(x, dim=-1)
    if op == "topk":
        x = torch.randn(shape["rows"], shape["cols"], dtype=dtype, device=device)
        return lambda: torch.topk(x, int(params["k"]), dim=-1)

    if op in {"matmul", "addmm"}:
        x = torch.randn(shape["m"], shape["k"], dtype=dtype, device=device)
        weight = torch.randn(shape["k"], shape["n"], dtype=dtype, device=device)
        if op == "matmul":
            return lambda: torch.matmul(x, weight)
        bias = torch.randn(shape["n"], dtype=dtype, device=device)
        return lambda: torch.addmm(bias, x, weight)
    if op == "batch_matmul":
        x = torch.randn(shape["batch"], shape["m"], shape["k"], dtype=dtype, device=device)
        weight = torch.randn(shape["batch"], shape["k"], shape["n"], dtype=dtype, device=device)
        return lambda: torch.bmm(x, weight)
    if op == "conv2d":
        x = torch.randn(shape["batch"], shape["in_channels"], shape["height"], shape["width"], dtype=dtype, device=device)
        weight = torch.randn(shape["out_channels"], shape["in_channels"], shape["kernel"], shape["kernel"], dtype=dtype, device=device)
        padding = int(shape["kernel"]) // 2
        return lambda: torch.nn.functional.conv2d(x, weight, padding=padding)
    if op == "conv3d":
        x = torch.randn(shape["batch"], shape["in_channels"], shape["depth"], shape["height"], shape["width"], dtype=dtype, device=device)
        weight = torch.randn(shape["out_channels"], shape["in_channels"], shape["kernel"], shape["kernel"], shape["kernel"], dtype=dtype, device=device)
        padding = int(shape["kernel"]) // 2
        return lambda: torch.nn.functional.conv3d(x, weight, padding=padding)
    if op == "quant_batch_matmul":
        operation = _require_npu_op(torch, "npu_quant_matmul")
        x = torch.randint(-8, 8, (shape["m"], shape["k"]), dtype=torch.int8, device=device)
        weight = torch.randint(-8, 8, (shape["k"], shape["n"]), dtype=torch.int8, device=device)
        scale = torch.ones(shape["n"], dtype=torch.float32, device=device)
        return lambda: operation(x, weight, scale, output_dtype=torch.bfloat16)

    if op == "layer_norm":
        x = torch.randn(shape["rows"], shape["hidden"], dtype=dtype, device=device)
        weight = torch.ones(shape["hidden"], dtype=dtype, device=device)
        bias = torch.zeros(shape["hidden"], dtype=dtype, device=device)
        return lambda: torch.nn.functional.layer_norm(x, (shape["hidden"],), weight, bias)
    if op in {"rms_norm", "add_rms_norm"}:
        operation = _require_npu_op(torch, f"npu_{op}")
        x = torch.randn(shape["rows"], shape["hidden"], dtype=dtype, device=device)
        gamma = torch.ones(shape["hidden"], dtype=dtype, device=device)
        if op == "rms_norm":
            return lambda: operation(x, gamma, 1e-6)
        residual = torch.randn_like(x)
        return lambda: operation(x, residual, gamma, 1e-6)
    if op == "swiglu":
        operation = _require_npu_op(torch, "npu_swiglu")
        x = torch.randn(shape["rows"], shape["hidden"] * 2, dtype=dtype, device=device)
        return lambda: operation(x, -1)
    if op == "rotary_mul":
        operation = _require_npu_op(torch, "npu_rotary_mul")
        x = torch.randn(shape["batch"], shape["seq"], shape["heads"], shape["head_dim"], dtype=dtype, device=device)
        cosine = torch.randn(1, shape["seq"], 1, shape["head_dim"], dtype=dtype, device=device)
        sine = torch.randn_like(cosine)
        return lambda: operation(x, cosine, sine, "half")
    if op == "fusion_attention":
        operation = _require_npu_op(torch, "npu_fusion_attention")
        tensor_shape = (shape["batch"], shape["seq"], shape["heads"], shape["head_dim"])
        query = torch.randn(*tensor_shape, dtype=dtype, device=device)
        key = torch.randn_like(query)
        value = torch.randn_like(query)
        scale = float(shape["head_dim"]) ** -0.5
        return lambda: operation(query, key, value, shape["heads"], "BSND", scale=scale)
    if op == "prompt_flash_attention":
        operation = _require_npu_op(torch, "npu_prompt_flash_attention")
        hidden = shape["heads"] * shape["head_dim"]
        query = torch.randn(shape["batch"], shape["seq"], hidden, dtype=dtype, device=device)
        key = torch.randn_like(query)
        value = torch.randn_like(query)
        scale = float(shape["head_dim"]) ** -0.5
        return lambda: operation(query, key, value, num_heads=shape["heads"], num_key_value_heads=shape["heads"], scale_value=scale, input_layout="BSH", next_tokens=shape["seq"])
    if op == "incre_flash_attention":
        operation = _require_npu_op(torch, "npu_incre_flash_attention")
        q_hidden = shape["heads"] * shape["head_dim"]
        kv_hidden = shape["kv_heads"] * shape["head_dim"]
        query = torch.randn(shape["batch"], 1, q_hidden, dtype=dtype, device=device)
        key = torch.randn(shape["batch"], shape["kv_seq"], kv_hidden, dtype=dtype, device=device)
        value = torch.randn_like(key)
        lengths = [shape["kv_seq"]] * shape["batch"]
        scale = float(shape["head_dim"]) ** -0.5
        return lambda: operation(query, key, value, actual_seq_lengths=lengths, num_heads=shape["heads"], num_key_value_heads=shape["kv_heads"], scale_value=scale, input_layout="BSH")
    if op == "ffn":
        operation = _require_npu_op(torch, "npu_ffn")
        x = torch.randn(shape["tokens"], shape["hidden"], dtype=dtype, device=device)
        weight1 = torch.randn(shape["hidden"], shape["intermediate"], dtype=dtype, device=device)
        weight2 = torch.randn(shape["intermediate"], shape["hidden"], dtype=dtype, device=device)
        return lambda: operation(x, weight1, weight2, str(params["activation"]))
    if op == "grouped_matmul":
        operation = _require_npu_op(torch, "npu_grouped_matmul")
        xs = [torch.randn(shape["m"], shape["k"], dtype=dtype, device=device) for _ in range(shape["groups"])]
        weights = [torch.randn(shape["k"], shape["n"], dtype=dtype, device=device) for _ in range(shape["groups"])]
        return lambda: operation(xs, weights, split_item=0)
    if op in {"npu_top_k_top_p", "custom_top_k_top_p"}:
        if op == "custom_top_k_top_p":
            _register_custom_opp()
            from vllm_ascend.utils import enable_custom_op

            if not enable_custom_op():
                raise UnsupportedOperator("vLLM Ascend custom operators are disabled")
        logits = torch.empty(shape["batch"], shape["vocab"], dtype=dtype, device=device).uniform_(-5, 5)
        k = torch.full((shape["batch"],), int(params["top_k"]), dtype=torch.int32, device=device)
        p = torch.full((shape["batch"],), float(params["top_p"]), dtype=dtype, device=device)
        if op == "npu_top_k_top_p":
            operation = _require_npu_op(torch, "npu_top_k_top_p")
            return lambda: operation(logits, p, k)
        return lambda: torch.ops._C_ascend.npu_apply_top_k_top_p(logits, k=k, p=p)
    raise UnsupportedOperator(f"no builder registered for {op}")


def emit(messages: Any, kind: str, role: str, rank: int, detail: str = "") -> None:
    messages.put((kind, role, rank, detail))


def model_a(rank: int, port: int, start: Any, stop_requested: Any, messages: Any) -> None:
    try:
        os.environ.update(XLITE_NODE_IPS="127.0.0.1", XLITE_PORT=str(port), XLITE_DISABLE_XCCL="false")
        import torch
        import torch_npu  # noqa: F401
        from xlite._C import Runtime, all_reduce

        torch.npu.set_device(rank)
        runtime = Runtime(rank, 128, rank, TP, 1)
        source = torch.ones(ALLREDUCE_SHAPE, dtype=torch.bfloat16, device=f"npu:{rank}")
        output = torch.empty_like(source)
        torch.npu.synchronize()
        emit(messages, "READY", "A", rank, f"npu={rank}")
        start.wait()
        if rank != 0:
            stop_requested.wait()
            return
        emit(messages, "ENTER", "A", rank, "all_reduce count=20480 dtype=bf16")
        all_reduce(runtime, output, source, 0)
        emit(messages, "SUBMITTED", "A", rank, "host call returned")
        torch.npu.synchronize()
        emit(messages, "DONE", "A", rank, "unexpected collective completion")
    except BaseException:
        emit(messages, "ERROR", "A", rank, traceback.format_exc())


def model_x(scenario: Scenario, start: Any, messages: Any) -> None:
    stage = "import"
    try:
        if scenario.operator == "custom_top_k_top_p":
            _register_custom_opp()
        import torch
        import torch_npu

        stage = "device"
        torch.npu.set_device(0)
        stage = "build"
        operation = build_operation(torch, torch_npu, scenario, "npu:0")
        stage = "warmup"
        began = time.monotonic()
        operation()
        torch.npu.synchronize()
        warmup = time.monotonic() - began
        emit(messages, "READY", "X", 0, f"warmup_elapsed={warmup:.6f}s")
        start.wait()
        stage = "measured"
        began = time.monotonic()
        operation()
        emit(messages, "SUBMITTED", "X", 0, "host call returned")
        torch.npu.synchronize()
        emit(messages, "DONE", "X", 0, f"elapsed={time.monotonic() - began:.6f}s")
    except UnsupportedOperator:
        emit(messages, "UNSUPPORTED", "X", 0, f"stage={stage}\n{traceback.format_exc()}")
    except BaseException:
        emit(messages, "ERROR", "X", 0, f"stage={stage}\n{traceback.format_exc()}")


class Events:
    def __init__(self, messages: Any) -> None:
        self.messages = messages
        self.items: list[tuple[str, str, int, str]] = []
        self.states: dict[tuple[str, int], str] = {}

    def receive(self, timeout: float) -> tuple[str, str, int, str] | None:
        try:
            item = self.messages.get(timeout=max(0.0, timeout))
        except queue.Empty:
            return None
        kind, role, rank, detail = item
        self.items.append(item)
        self.states[(role, rank)] = kind
        print(f"EVENT role={role} rank={rank} state={kind} detail={detail}", flush=True)
        return item

    def count(self, kind: str, role: str | None = None) -> int:
        return sum(item_kind == kind and (role is None or item_role == role) for item_kind, item_role, _rank, _detail in self.items)

    def until(self, kind: str, count: int, timeout: float, role: str | None = None) -> bool:
        deadline = time.monotonic() + timeout
        while self.count(kind, role) < count:
            if self.count("ERROR") or self.count("UNSUPPORTED"):
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0 or self.receive(remaining) is None:
                break
        return self.count(kind, role) >= count

    def collect_for(self, duration: float) -> None:
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if self.receive(deadline - time.monotonic()) is None:
                return

    def detail(self, kind: str, role: str) -> str:
        return "\n".join(detail for item_kind, item_role, _rank, detail in self.items if item_kind == kind and item_role == role)


def stop_processes(processes: list[mp.Process], stop_requested: Any) -> None:
    stop_requested.set()
    for process in processes:
        process.join(timeout=0.5)
    for process in processes:
        if process.is_alive():
            process.terminate()
    for process in processes:
        process.join(timeout=5)
    for process in processes:
        if process.is_alive():
            process.kill()
            process.join(timeout=2)


def execute_case(scenario: Scenario, phase: str, settings: Settings, attempt: int) -> dict[str, Any]:
    if sys.platform != "linux":
        raise RuntimeError("single cases must run inside the Linux Ascend container")
    for name in ("RANK", "LOCAL_RANK", "WORLD_SIZE", "MASTER_ADDR", "MASTER_PORT"):
        os.environ.pop(name, None)
    os.environ.setdefault("ASCEND_SLOG_PRINT_TO_STDOUT", "0")
    os.environ.setdefault("ASCEND_GLOBAL_LOG_LEVEL", "3")

    ctx = mp.get_context("spawn")
    messages = ctx.Queue()
    events = Events(messages)
    x_start = ctx.Event()
    a_start = ctx.Event()
    stop_requested = ctx.Event()
    processes: list[mp.Process] = []
    started = time.monotonic()
    result = "SETUP_FAILED"
    error = ""
    try:
        if phase == "contention":
            port = free_xlite_port()
            processes.extend(ctx.Process(target=model_a, name=f"model-A-rank-{rank}", args=(rank, port, a_start, stop_requested, messages)) for rank in range(TP))
            for process in processes:
                process.start()
            if not events.until("READY", TP, settings.startup_timeout, role="A"):
                error = events.detail("ERROR", "A")
                result = "SETUP_FAILED"
                return _record(scenario, phase, settings, attempt, result, started, events, error)

        x_process = ctx.Process(target=model_x, name="model-X-rank-0", args=(scenario, x_start, messages))
        x_process.start()
        processes.append(x_process)
        if not events.until("READY", 1, settings.startup_timeout, role="X"):
            x_error = events.detail("ERROR", "X")
            if events.count("UNSUPPORTED", role="X") or x_error.startswith(("stage=build", "stage=warmup")):
                result = "UNSUPPORTED"
                error = events.detail("UNSUPPORTED", "X") or x_error
            else:
                result = "SETUP_FAILED"
                error = x_error or "X warmup timed out"
            return _record(scenario, phase, settings, attempt, result, started, events, error)

        if phase == "contention":
            a_start.set()
            if not events.until("ENTER", 1, settings.operator_timeout, role="A"):
                result = "SETUP_FAILED"
                error = events.detail("ERROR", "A") or "A rank 0 did not enter AllReduce"
                return _record(scenario, phase, settings, attempt, result, started, events, error)
            events.collect_for(settings.hold_confirm_seconds)
            if events.count("DONE", role="A") or events.count("ERROR", role="A"):
                result = "SETUP_FAILED"
                error = events.detail("ERROR", "A") or "AllReduce did not remain blocked"
                return _record(scenario, phase, settings, attempt, result, started, events, error)

        x_start.set()
        completed = events.until("DONE", 1, settings.operator_timeout, role="X")
        if events.count("ERROR", role="X") or events.count("UNSUPPORTED", role="X"):
            result = "RUNTIME_FAILED"
            error = events.detail("ERROR", "X") or events.detail("UNSUPPORTED", "X")
        elif phase == "contention" and events.count("DONE", role="A"):
            result = "SETUP_FAILED"
            error = "AllReduce did not stay blocked"
        elif completed:
            result = "PASS"
        elif phase == "contention":
            result = "BLOCKED_BY_A_ALLREDUCE"
        else:
            result = "RUNTIME_FAILED"
            error = "X_ONLY measured invocation timed out"
        return _record(scenario, phase, settings, attempt, result, started, events, error)
    finally:
        stop_processes(processes, stop_requested)
        messages.close()


def _record(scenario: Scenario, phase: str, settings: Settings, attempt: int, result: str, started: float, events: Events, error: str) -> dict[str, Any]:
    return {
        "scenario": scenario.id,
        "operator": scenario.operator,
        "kind": scenario.kind,
        "expected_core": scenario.expected_core,
        "dtype": scenario.dtype,
        "shape": dict(scenario.shape),
        "params": dict(scenario.params),
        "source": scenario.source,
        "phase": phase,
        "attempt": attempt,
        "result": result,
        "elapsed_seconds": round(time.monotonic() - started, 6),
        "warmup_state": "READY" if events.count("READY", role="X") else "NOT_READY",
        "a_state": events.states.get(("A", 0), "NOT_APPLICABLE" if phase == "preflight" else "NOT_STARTED"),
        "x_state": events.states.get(("X", 0), "NOT_STARTED"),
        "error": error,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "environment": environment_fingerprint(),
        "timeouts": asdict(settings),
    }
