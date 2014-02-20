#include "thorin/be/llvm/nvvm.h"
#include <sstream>
#include <llvm/IR/Function.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>

#include "thorin/literal.h"
#include "thorin/world.h"
#include "thorin/memop.h"

namespace thorin {

NVVMCodeGen::NVVMCodeGen(World& world)
    : CodeGen(world, llvm::CallingConv::PTX_Device)
{
    module_->setDataLayout("e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v32:32:32-v64:64:64-v128:128:128-n16:32:64");
}

//------------------------------------------------------------------------------
// Kernel code
//------------------------------------------------------------------------------

static AddressSpace resolve_addr_space(Def def) {
    if (auto ptr = def->type()->isa<Ptr>())
        return ptr->addr_space();
    return AddressSpace::Global;
}

llvm::Function* NVVMCodeGen::emit_function_decl(std::string& name, Lambda* lambda) {
    // skip non-global address-space parameters
    std::vector<const Type*> types;
    for (auto type : lambda->type()->elems()) {
        if (auto ptr = type->isa<Ptr>())
            if (ptr->addr_space() != AddressSpace::Global)
                continue;
        types.push_back(type);
    }
    auto ft = llvm::cast<llvm::FunctionType>(map(lambda->world().pi(types)));
    auto f = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, lambda->name, module_);

    if (!lambda->attribute().is(Lambda::KernelEntry))
        return f;

    // append required metadata
    auto annotation = module_->getOrInsertNamedMetadata("nvvm.annotations");

    const auto append_metadata = [&](llvm::Value* target, const std::string &name) {
        llvm::Value* annotation_values[] = { target, llvm::MDString::get(context_, name), builder_.getInt64(1) };
        llvm::MDNode* result = llvm::MDNode::get(context_, annotation_values);
        annotation->addOperand(result);
        return result;
    };

    const auto emit_texture_kernel_arg = [&](const Param* param) {
        assert(param->type()->as<Ptr>()->addr_space() == AddressSpace::Texture);
        auto global = new llvm::GlobalVariable(*module_.get(), builder_.getInt64Ty(), false,
                llvm::GlobalValue::InternalLinkage, builder_.getInt64(0), param->unique_name(),
                nullptr, llvm::GlobalVariable::NotThreadLocal, 1);
        metadata_[param] = append_metadata(global, "texture");
    };

    append_metadata(f, "kernel");
    f->setCallingConv(llvm::CallingConv::PTX_Kernel);

    // check signature for texturing memory
    for (auto param : lambda->params()) {
        if (auto ptr = param->type()->isa<Ptr>()){
            switch (ptr->addr_space()) {
            case AddressSpace::Texture:
                emit_texture_kernel_arg(param);
                break;
            case AddressSpace::Shared:
                assert(false && "Shared address space is TODO");
                break;
            default:
                // ignore this address space
                break;
            }
        }
    }

    return f;
}

llvm::Value* NVVMCodeGen::map_param(llvm::Function*, llvm::Argument* arg, const Param* param) {
    if (!param->lambda()->attribute().is(Lambda::KernelEntry))
        return arg;
    else if (auto var = resolve_global_variable(param))
        return var;
    return arg;
}

llvm::Function* NVVMCodeGen::get_texture_handle_fun() {
    // %tex_ref = call i64 @llvm.nvvm.texsurf.handle.p1i64(metadata !{i64 addrspace(1)* @texture, metadata !"texture", i32 1}, i64 addrspace(1)* @texture)
    llvm::Type* types[2] = {
            llvm::Type::getMetadataTy(context_),
            llvm::PointerType::get(builder_.getInt64Ty(), 1)
    };
    auto type = llvm::FunctionType::get(builder_.getInt64Ty(), types, false);
    return llvm::cast<llvm::Function>(module_->getOrInsertFunction("llvm.nvvm.texsurf.handle.p1i64", type));
}

void NVVMCodeGen::emit_function_start(llvm::BasicBlock* bb, llvm::Function* f, Lambda* lambda) {
    if (!lambda->attribute().is(Lambda::KernelEntry))
        return;
    // kernel needs special setup code for the arguments
    auto texture_handle = get_texture_handle_fun();
    for (auto param : lambda->params()) {
        if (auto var = resolve_global_variable(param)) {
            auto md = metadata_.find(param);
            assert(md != metadata_.end());
            // require specific handle to be mapped to a parameter
            llvm::Value* args[] = { md->second, var };
            params_[param] = builder_.CreateCall(texture_handle, args);
        }
    }
}

llvm::Function* NVVMCodeGen::emit_intrinsic_decl(std::string& name, Lambda* lambda) {
    auto f = CodeGen::emit_function_decl(name, lambda);
    f->setAttributes(llvm::AttributeSet());
    return f;
}

llvm::Value* NVVMCodeGen::emit_load(Def def) {
    auto load = def->as<Load>();
    switch (resolve_addr_space(load->ptr())) {
    case AddressSpace::Texture:
        return builder_.CreateExtractValue(lookup(load->ptr()), { unsigned(0) });
    case AddressSpace::Shared:
        THORIN_UNREACHABLE;
    default:
        return CodeGen::emit_load(def);
    }
}

llvm::Value* NVVMCodeGen::emit_store(Def def) {
    auto store = def->as<Store>();
    assert(resolve_addr_space(store->ptr()) == AddressSpace::Global &&
            "Only global address space for stores is currently supported");
    return CodeGen::emit_store(store);
}

static std::string get_texture_fetch_command(const Type* type) {
    std::stringstream fun_str;
    if (type->as<PrimType>()->is_type_f())
        fun_str << "tex.1d.v4.f32.s32";
    else
        fun_str << "tex.1d.v4.s32.s32";
    fun_str << " {$0,$1,$2,$3}, [$4, {$5,$6,$7,$8}];";
    return fun_str.str();
}

llvm::Value* NVVMCodeGen::emit_lea(Def def) {
    auto lea = def->as<LEA>();
    switch (resolve_addr_space(lea->ptr())) {
    case AddressSpace::Texture: {
        // sample for i32:
        // %tex_fetch = call { i32, i32, i32, i32 } asm sideeffect "tex.1d.v4.s32.s32 {$0,$1,$2,$3}, [$4, {$5,$6,$7,$8}];",
        // "=r,=r,=r,=r,l,r,r,r,r" (i64 %tex_ref, i32 %add, i32 0, i32 0, i32 0)
        auto ptr_ty = lea->type()->as<Ptr>();
        auto llvm_ptr_ty = map(ptr_ty->referenced_type());
        llvm::Type* struct_types[] = { llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty, llvm_ptr_ty };
        auto ret_type = llvm::StructType::create(struct_types);
        llvm::Type* args[] = {
            builder_.getInt64Ty(),
            builder_.getInt32Ty(), builder_.getInt32Ty(), builder_.getInt32Ty(), builder_.getInt32Ty() };
        auto type = llvm::FunctionType::get(ret_type, args, false);
        auto fetch_command = get_texture_fetch_command(ptr_ty->referenced_type());
        auto get_call = llvm::InlineAsm::get(type, fetch_command, "=r,=r,=r,=r,l,r,r,r,r", false);
        llvm::Value* values[] = {
            lookup(lea->ptr()), lookup(lea->index()),
            builder_.getInt32(0), builder_.getInt32(0), builder_.getInt32(0) };
        return builder_.CreateCall(get_call, values);
    }
    default:
        return CodeGen::emit_lea(def);
    }
}

llvm::GlobalVariable* NVVMCodeGen::resolve_global_variable(const Param* param) {
    if (resolve_addr_space(param) != AddressSpace::Global)
        return module_->getGlobalVariable(param->unique_name(), true);
    return nullptr;
}

}
