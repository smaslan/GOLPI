//------------------------------------------------------------------------------
// Script for transfering variables to Octave environment via named pipes.
//
// Data format to be send by caller:
//   DWORD - variable_type_id
//   DWORD - rows_count
//   DWORD - columns_count
//   BYTES - variable data
// 
// Usage:
//   [var_name] = golpi_pipe_receive(pipe_name)
//   [var_name] = golpi_pipe_receive(pipe_name, timeout)
//
// Parameters:
//   pipe_name: Windows named pipe that has to be created by caller beforehand
//              e.g. '\\.\Pipe\GOLPI_data_pipe'
//   timeout: Total data read timeout value [s] (optional)
// 
// (c) 2025, Stanislav Maslan, smaslan@cmi.cz
//------------------------------------------------------------------------------
#include <octave/oct.h>
#include <windows.h>
#include "golpi_pipe.hpp"


// send variable 
DEFUN_DLD(golpi_test, args, nargout, "Transfer variable from Octave using named pipe")
{
    octave_value_list res;    
        
    // identify data type
    auto var = args(0);
    std::string errstr;
    DWORD var_type = VTYPE_ERROR;
    if(var.iscell())
        errstr = "GOLPI pipe interface: Cell variables not supported.";
    else if(var.ndims() > 2)
        errstr = "GOLPI pipe interface: Variable must have max 2 dims.";
    else if(!var.isnumeric() && !var.is_string())
        errstr = "GOLPI pipe interface: Variable must be numeric type or string.";
    else if(var.is_string())
        var_type = VTYPE_STRING;    
    else if(var.iscomplex())
    {
        if(var.is_double_type())
            var_type = VTYPE_CDBL;
        else if(var.is_single_type())
            var_type = VTYPE_CSGL;
        else
            errstr = "GOLPI pipe interface: complex variables must be single or double.";
    }
    else if(var.is_double_type())
        var_type = VTYPE_DBL;
    else if(var.is_single_type())
        var_type = VTYPE_SGL;
    else if(var.is_int32_type())
        var_type = VTYPE_INT32;
    else if(var.is_uint32_type())
        var_type = VTYPE_UINT32;
    else
        errstr = "GOLPI pipe interface: unsupported variable type.";
        
    // data element size
    DWORD element_size;
    switch(var_type)
    {
        case VTYPE_STRING: element_size = 1; break;
        case VTYPE_INT8: element_size = 1; break;
        case VTYPE_UINT8: element_size = 1; break;
        case VTYPE_INT16: element_size = sizeof(WORD); break;
        case VTYPE_UINT16: element_size = sizeof(WORD); break;
        case VTYPE_INT32: element_size = sizeof(DWORD); break;
        case VTYPE_UINT32: element_size = sizeof(DWORD); break;
        case VTYPE_DBL: element_size = sizeof(double); break;
        case VTYPE_CDBL: element_size = 2*sizeof(double); break;
        case VTYPE_SGL: element_size = sizeof(float); break;
        case VTYPE_CSGL: element_size = 2*sizeof(float); break;
        default: element_size = 0;
    }
    
    
    octave_stdout << "type = " << var_type << ", byte_size = "<< var.byte_size() << ", value=" << ((float*)var.mex_get_data())[1] <<"\n";
    
    return res;    
}


