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


// send ACK or NACK message
DWORD WaitACK(HANDLE file)
{
    char res;
    if(ReadFileTimeout(file, &res, 1, NULL, 1.0))
        return(0);
    if(WriteFileTimeout(file, &res, 1, NULL, 0, 0.1))      
        return(0);
    return(1);    
}

// send variable 
DEFUN_DLD(golpi_pipe_send, args, nargout, "Transfer variable from Octave using named pipe")
{
    octave_value_list res;
    
    // outputs
    if(nargout != 0)
        error("GOLPI pipe interface: No output arguments expected.");
    
    // try get pipe name
    if(args.length() < 2)
        error("GOLPI pipe interface: At least name of the pipe and variable to send must be passed.");                
    if(!args(0).is_string() || args(0).char_matrix_value().rows() != 1)
        error("GOLPI pipe interface: First argument must be pipe name string.");
    std::string pipe_name = args(0).char_matrix_value().row_as_string(0);
        
    // try get timeout parameter
    double timeout = 3.0;
    if(args.length() >= 3 && args(2).array_value().numel() == 1)
        timeout = args(2).array_value().elem(0);
    else if(args.length() >= 3)
        error("GOLPI pipe interface: Third parameter must be double timeout value [s].");
        
        
    // identify data type
    auto var = args(1);
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
    else if(var.is_int16_type())
        var_type = VTYPE_INT16;
    else if(var.is_uint16_type())
        var_type = VTYPE_UINT16;
    else if(var.is_int8_type())
        var_type = VTYPE_INT8;
    else if(var.is_uint8_type())
        var_type = VTYPE_UINT8;
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
    
    // get matrix size
    DWORD m = var.dims()(0);
    DWORD n = var.dims()(1);
    bool is_empty = !m && !n;
    
    if(DEBUG_PRN)
        octave_stdout << "var type = " << var_type << ", dims count = " << (int)var.ndims() << ", m = " << m << ", n = " << n << "\n";
        
    if(var_type == VTYPE_ERROR)
    {
        m = 0;
        n = 0;
    }
    
    // total data size
    DWORD data_size_bytes = m*n*element_size; 
            
    // try open pipe
    HANDLE hPipe;
    hPipe = CreateFileA(
		    pipe_name.c_str(), 
		    GENERIC_READ | GENERIC_WRITE, // access
        0, // sharing
        NULL, // security
        OPEN_EXISTING, // create mode
        FILE_FLAG_OVERLAPPED, // other
        NULL // template
        );         
  	if (hPipe == INVALID_HANDLE_VALUE)
        error("GOLPI pipe interface: Cannot access data pipe.");
        
    // get pipe buffer sizes
    DWORD out_buf_size,in_buf_size;
    GetNamedPipeInfo(hPipe, NULL, &out_buf_size, &in_buf_size, NULL);    
    if(DEBUG_PRN)
        octave_stdout << "out_buf_size = " << out_buf_size << ", in_buf_size = " << in_buf_size << "\n";
    
    // default write block size (must be smaller than buffer size!)
    DWORD write_block = 0.9*in_buf_size;    
        
    // sync with caller
    char sync;
    ReadFileTimeout(hPipe, &sync, 1, NULL, 1.0);                     
        
    // send minimal response even when error occured
    DWORD written;
    if(WriteFileTimeout(hPipe, &var_type, sizeof(DWORD), &written, write_block, timeout))
    {
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Cannot write variable type code to pipe");
    }
    if(WriteFileTimeout(hPipe, &m, sizeof(DWORD), &written, write_block, timeout))
    {
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Cannot write M size to pipe");
    }
    if(WriteFileTimeout(hPipe, &n, sizeof(DWORD), &written, write_block, timeout))
    {
        CloseHandle(hPipe);
        error("GOLPI pipe interface: Cannot write N size to pipe");
    }
    
    if(var_type == VTYPE_ERROR)
    {
        // error
        WaitACK(hPipe);
        CloseHandle(hPipe);
        error(errstr.c_str());
    }
    
    if(!is_empty)
    {    
        if(WriteFileTimeoutACK(hPipe, var.array_value().fortran_vec(), data_size_bytes, &written, write_block, timeout))
        {
            // timeout - error
            CloseHandle(hPipe);
            error("GOLPI pipe interface: Timeout while transfering variable data.");
        }
    }
    
    // wait for ACK
    WaitACK(hPipe);
    CloseHandle(hPipe);
    
    // console sync mark
    octave_stdout << "GOLPImark\n";
    return res;    
}


