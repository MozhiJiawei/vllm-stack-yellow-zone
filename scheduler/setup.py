from setuptools import Extension, find_packages, setup

setup(
    packages=find_packages("src"),
    package_dir={"": "src"},
    ext_modules=[
        Extension(
            "vllm_pair_scheduler._pair_sched_native",
            ["src/vllm_pair_scheduler/native/pair_sched.c"],
            extra_compile_args=["-std=c11", "-O2", "-Wall", "-Wextra"],
            extra_link_args=["-pthread"],
        )
    ],
)
