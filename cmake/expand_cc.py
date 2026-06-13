#!/usr/bin/env python3
"""
展开 compile_commands.json 中的 @xxx.rsp 响应文件引用。

CMake 在 Windows 上使用响应文件传递编译参数，
clangd 无法解析 @file.rsp 语法导致代码跳转失败。
此脚本将 @rsp 引用替换为实际文件内容。
"""
import json, re, os, sys

def expand(build_dir: str, output_path: str):
    cc_file = os.path.join(build_dir, 'compile_commands.json')
    if not os.path.exists(cc_file):
        print(f"ERROR: {cc_file} not found")
        sys.exit(1)

    with open(cc_file, 'r', encoding='utf-8') as f:
        data = json.load(f)

    count = 0
    for entry in data:
        cmd = entry['command']
        new_cmd, n = re.subn(r'@(\S+\.rsp)', lambda m: _read_rsp(entry['directory'], m.group(1)), cmd)
        if n > 0:
            entry['command'] = new_cmd
            count += n

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)

    print(f"compile_commands.json → {output_path}  ({count} 个 @rsp 已展开)")

def _read_rsp(work_dir: str, rsp_rel: str) -> str:
    full = os.path.join(work_dir, rsp_rel)
    if os.path.exists(full):
        with open(full, 'r') as f:
            return f.read().strip()
    return f'@{rsp_rel}'

if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser()
    p.add_argument('build_dir')
    p.add_argument('-o', '--output', default='compile_commands.json')
    args = p.parse_args()
    expand(args.build_dir, args.output)
