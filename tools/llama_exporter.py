# The MIT License (MIT)
#
# Copyright (c) 2023 Xiaoyang Chen
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software
# and associated documentation files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all copies or
# substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import argparse
import torch
import configparser
import zipfile
import io
import sys
from os import path
from model_exporter import Context, ModelExporter, TensorWriter, Quant
from bpe_exporter import read_spm_model, read_transformers_fast_bpe_model
from torch import nn

class LlamaExporter(ModelExporter):
    def __init__(self, writer: TensorWriter) -> None:
        super().__init__(writer)

    def _export(self, ctx: Context, model):
        base_model = model.base_model

        self.export_embedding(ctx.with_subname("embd"), base_model.embed_tokens)
        self._export_rms_norm(ctx.with_subname("norm"), base_model.norm)
        self._export_rope(ctx.with_subname("rope"), base_model.layers[0].self_attn.rotary_emb)
        for idx, block in enumerate(base_model.layers):
            self._export_block(ctx.with_subname("block" + str(idx)), block)
        self._write(ctx.with_subname("out_proj.weight"), model.lm_head.weight)

    def _export_rms_norm(self, ctx: Context, module):
        self._write(ctx.with_subname("weight").with_quant(Quant.NONE), module.weight)

    def _export_block(self, ctx: Context, model_block):
        self._export_rms_norm(ctx.with_subname("input_norm"), model_block.input_layernorm)
        self._export_attn(ctx.with_subname("attn"), model_block.self_attn)
        self._export_rms_norm(ctx.with_subname("post_attn_norm"), model_block.post_attention_layernorm)
        self._export_mlp(ctx.with_subname("mlp"), model_block.mlp)

    def _export_rope(self, ctx: Context, rope_embd):
        original_max_seq_len = rope_embd.original_max_seq_len
        position_ids = torch.arange(original_max_seq_len).unsqueeze(0)
        x = torch.ones(1)

        cos_cached, sin_cached = rope_embd(x, position_ids)

        rope = torch.stack((cos_cached, sin_cached))
        self._write(ctx.with_quant(Quant.NONE), rope)

    def _export_attn(self, ctx: Context, attn_block):
        q_proj = attn_block.q_proj.weight
        k_proj = attn_block.k_proj.weight
        v_proj = attn_block.v_proj.weight

        qkv_proj = torch.cat((q_proj, k_proj, v_proj), dim=0)
        self._write(ctx.with_subname("qkv_proj.weight"), qkv_proj)
        self._write(ctx.with_subname("out_proj.weight"), attn_block.o_proj.weight)

    def _export_mlp(self, ctx: Context, attn_block):
        w_gate = attn_block.gate_proj.weight
        w_up = attn_block.up_proj.weight
        w_gate_up = torch.cat((w_gate, w_up), dim=0)
        self._write(ctx.with_subname("gate_up_proj.weight"), w_gate_up)
        self._write(ctx.with_subname("down_proj.weight"), attn_block.down_proj.weight)

    @classmethod
    def generate_config(cls, llama_config) -> configparser.ConfigParser:
        config = configparser.ConfigParser()
        config["llama"] = {}

        print("llama_config.rope_scaling =", llama_config.rope_scaling)

        assert llama_config.pretraining_tp == 1
        assert llama_config.hidden_act == "silu"
        
        section = config["llama"]
        section["hidden_size"] = str(llama_config.hidden_size)
        section["num_heads"] = str(llama_config.num_attention_heads)
        section["num_key_value_heads"] = str(llama_config.num_key_value_heads)
        section["intermediate_size"] = str(llama_config.intermediate_size)
        section["norm_eps"] = str(llama_config.rms_norm_eps)
        section["num_layers"] = str(llama_config.num_hidden_layers)
        section["vocab_size"] = str(llama_config.vocab_size)
        section["max_ctx_length"] = str(llama_config.max_position_embeddings)
        section["bot_token_id"] = "1"
        section["eot_token_id"] = "2"

        return config

    @classmethod
    def export(cls, llama_model, fp, quant: Quant):
        model = llama_model
        config = llama_model.config

        ctx = Context("llama", quant=quant)
        with TensorWriter(fp) as writer:
            exporter = LlamaExporter(writer)
            exporter._export(ctx, model)

        ini_config = cls.generate_config(config)
        ini_config["model"] = {}
        ini_config["model"]["type"] = "llama"
        ini_config["model"]["model_file"] = path.basename(MODEL_BIN)

        return ini_config

def run_llama_chat(huggingface_name):
    from transformers import AutoModelForCausalLM, AutoTokenizer
    tokenizer = AutoTokenizer.from_pretrained(huggingface_name, use_fast=False)

    prompt = "hi<s>"
    messages = [
        {"role": "user", "content": prompt}
    ]
    model_inputs = tokenizer.apply_chat_template(messages, add_generation_prompt=True, return_tensors="pt")
    model = AutoModelForCausalLM.from_pretrained(huggingface_name, device_map="cpu")
    terminators = [
        tokenizer.eos_token_id,
        tokenizer.convert_tokens_to_ids("<|eot_id|>")
    ]
    generated_ids = model.generate(model_inputs.input_ids, max_new_tokens=512)
    generated_ids = [
        output_ids[len(input_ids):] for input_ids, output_ids in zip(model_inputs.input_ids, generated_ids)
    ]
    print(generated_ids)

    response = tokenizer.batch_decode(generated_ids, skip_special_tokens=True)[0]
    print(response)

MODEL_NAME = "meta-llama/Llama-3.2-3B-Instruct"
MODEL_BIN = "model.bin"
MODEL_INI = "model.ini"
TOKENIZER_BIN = "tokenizer.bin"
TOKENIZER_INI = "tokenizer.ini"

if __name__ == '__main__':
    from transformers import AutoTokenizer
    from transformers.models.llama import LlamaForCausalLM


    parser = argparse.ArgumentParser(description='export llama model from huggingface to libllm format.')
    parser.add_argument('-huggingface_name', type=str, help='the llama model name in huggingface.', default=MODEL_NAME)
    parser.add_argument('-quant', type=Quant.parse, help='quantization type, "q4" or "none"', default=Quant.Q4)
    parser.add_argument('-output', type=str, help='output file name.', default="llama.llmpkg")
    parser.add_argument('-llama_version', type=int, help='llama model version.', default=3)
    parser.add_argument('-run', action="store_true")
    args = parser.parse_args()

    if args.run:
        run_llama_chat(args.huggingface_name)
        sys.exit(0)

    tokenizer = AutoTokenizer.from_pretrained(args.huggingface_name, trust_remote_code=True)
    model = LlamaForCausalLM.from_pretrained(args.huggingface_name, trust_remote_code=True)
    model = model.eval()

    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_STORED) as package:
        if args.llama_version == 3:
            libllm_tokenizer = read_transformers_fast_bpe_model(args.huggingface_name)
        else:
            libllm_tokenizer = read_spm_model(args.huggingface_name)

        with package.open(MODEL_BIN, "w", force_zip64=True) as fp:
            config = LlamaExporter.export(model, fp, args.quant)

        if args.llama_version == 3:
            config["llama"]["bot_token_id"] = str(tokenizer.bos_token_id)
            config["llama"]["eot_token_id"] = str(tokenizer.eos_token_id)
            config["llama"]["eot_id"] = str(tokenizer.convert_tokens_to_ids("<|eot_id|>"))

        with package.open(MODEL_INI, "w", force_zip64=True) as fp:
            config.write(io.TextIOWrapper(fp))

        with package.open(TOKENIZER_BIN, "w", force_zip64=True) as fp:
            libllm_tokenizer.save(fp)
        
        with package.open(TOKENIZER_INI, "w", force_zip64=True) as fp:
            libllm_tokenizer.get_config().to_ini(TOKENIZER_BIN).write(io.TextIOWrapper(fp))
