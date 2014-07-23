#ifndef __OPENCL_RT_HPP__
#define __OPENCL_RT_HPP__

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#include "thorin_runtime.h"

extern "C"
{

typedef size_t mem_id;

// SPIR wrappers
mem_id spir_malloc_buffer(size_t dev, void *host);
void spir_free_buffer(size_t dev, mem_id mem);

void spir_write_buffer(size_t dev, mem_id mem, void *host);
void spir_read_buffer(size_t dev, mem_id mem, void *host);

void spir_build_program_and_kernel_from_binary(size_t dev, const char *file_name, const char *kernel_name);
void spir_build_program_and_kernel_from_source(size_t dev, const char *file_name, const char *kernel_name);

void spir_set_kernel_arg(size_t dev, void *param, size_t size);
void spir_set_kernel_arg_map(size_t dev, mem_id mem);
void spir_set_kernel_arg_struct(size_t dev, void *param, size_t size);
void spir_set_problem_size(size_t dev, size_t size_x, size_t size_y, size_t size_z);
void spir_set_config_size(size_t dev, size_t size_x, size_t size_y, size_t size_z);

void spir_launch_kernel(size_t dev, const char *kernel_name);
void spir_synchronize(size_t dev);

// runtime functions
mem_id map_memory(size_t dev, size_t type_, void *from, int offset, int size);
void unmap_memory(size_t dev, size_t type_, mem_id mem);

}

#endif  // __OPENCL_RT_HPP__

