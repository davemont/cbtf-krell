/*******************************************************************************
** Copyright (c) 2012 Argo Navis Technologies. All Rights Reserved.
**
** This library is free software; you can redistribute it and/or modify it under
** the terms of the GNU Lesser General Public License as published by the Free
** Software Foundation; either version 2.1 of the License, or (at your option)
** any later version.
**
** This library is distributed in the hope that it will be useful, but WITHOUT
** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
** FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
** details.
**
** You should have received a copy of the GNU Lesser General Public License
** along with this library; if not, write to the Free Software Foundation, Inc.,
** 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*******************************************************************************/

/** @file Specification of the CUDA collector's blobs. */

%#include "KrellInstitute/Messages/Address.h"
%#include "KrellInstitute/Messages/File.h"
%#include "KrellInstitute/Messages/Thread.h"
%#include "KrellInstitute/Messages/Time.h"



/**
 * Enumeration of the different types of messages that are encapsulated within
 * this collector's blobs. See the note on CBTF_cuda_data for more information.
 */
enum CUDA_MessageTypes
{
    LoadedModule = 0,
    ResolvedFunction = 1,
    UnloadedModule = 2
};



/**
 * Message emitted when the CUDA driver loads a module.
 *
 * @note    The time_begin, time_end, addr_begin, and addr_end fields of the
 *          data header for this blob will all be zero for this message type.
 */
struct CUDA_LoadedModule
{
    /** Time at which the module was loaded. */
    CBTF_Protocol_Time time;
    
    /** Name of the file containing the module that was loaded. */
    CBTF_Protocol_FileName module;
    
    /** Handle within the CUDA driver of the loaded module .*/
    CBTF_Protocol_Address handle;
};



/**
 * Message emitted when the CUDA driver resolves a function.
 *
 * @note    The time_begin, time_end, addr_begin, and addr_end fields of the
 *          data header for this blob will all be zero for this message type.
 */
struct CUDA_ResolvedFunction
{
    /** Time at which the function was resolved. */
    CBTF_Protocol_Time time;
    
    /** Handle within the CUDA driver of the module containing the function. */
    CBTF_Protocol_Address module_handle;
    
    /** Name of the function being resolved. */
    string function<>;
    
    /** Handle within the CUDA Drvier of the resolved function. */
    CBTF_Protocol_Address handle;    
};



/**
 * Message emitted when the CUDA driver unloads a module.
 *
 * @note    The time_begin, time_end, addr_begin, and addr_end fields of the
 *          data header for this blob will all be zero for this message type.
 */
struct CUDA_UnloadedModule
{
    /** Time at which the module was unloaded. */
    CBTF_Protocol_Time time;
    
    /** Handle within the CUDA driver of the unloaded module .*/
    CBTF_Protocol_Address handle;
};



/**
 * Structure of the blob containing our performance data.
 *
 * @note    The CUDA driver contains a separate loader that is used to load
 *          binary code modules onto the GPU, resolve symbols, and invoke
 *          functions (kernels). Additionally, there are multiple types of
 *          performance data generated by this collector. These issues lead
 *          to this collector's performance data blobs being significantly
 *          more complex than those of the typical collector. To facilitate
 *          maximum reuse of existing collector infrastructure, all of the
 *          different data generated by this collector is "packed" into one
 *          performance data blob type using a XDR discriminated union.
 *
 * @sa http://en.wikipedia.org/wiki/Tagged_union
 */
union CBTF_cuda_data switch (unsigned type)
{
    case     LoadedModule:     CUDA_LoadedModule loaded_module;
    case ResolvedFunction: CUDA_ResolvedFunction resolved_function;
    case   UnloadedModule:   CUDA_UnloadedModule unloaded_module;

    default: void;
};









/**
 * Message containing performance data for a single CUDA kernel invocation.
 *
 * @note    This message is never directly emitted by the collector. It is
 *          <em>always</em> bundled together with other invocations into a 
 *          CUDA_KernelInvocations message.
 */
struct CUDA_KernelInvocation
{
    /**
     * Call site of the invocation. This is an index into the "stack_traces"
     * array of the parent CUDA_KernelInvocations.
     */
    uint32_t call_site;

    /** Name of the CUDA kernel invoked. */
    string kernel_name<>;

    /** Grid dimensions used for this invocation. */
    uint32_t grid_dimensions[3];

    /** Block dimensions used for this invocation. */
    uint32_t block_dimensions[3];

    /** Amount of dynamically allocated shared memory for this invocation. */
    uint64_t shared_memory;

    /** Start time of this invocation. */
    uint64_t start_time;

    /** Stop time of this invocation. */
    uint64_t stop_time;
};

/**
 * Message containing performance data for one or more CUDA kernel invocations.
 *
 * @note    When emitted by the collector this message is associated with the
 *          MRNet message tag CUDA_KERNEL_INVOCATIONS_TAG found in "tags.h".
 *
 * @note    Because CUDA kernel invocations typically occur from a limited set
 *          of call sites, grouping them together in this way (using a common
 *          list of stack traces) can result in significant data compression.
 */
struct CUDA_KernelInvocations
{
    /** List of unique, null-terminated, stack traces. */
    uint64_t stack_traces<>;
    
    /** Individual CUDA kernel invocation events. */
    CUDA_KernelInvocation events<>;
};
