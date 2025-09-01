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

// --- Timer stuff ---

// init interval timer
void timer_init(TTimer *timer)
{
    QueryPerformanceFrequency(&timer->freq);
    QueryPerformanceCounter(&timer->t_ref);  
}

// get elapsed time from timer_init() in seconds
double timer_get(TTimer *timer)
{
    LARGE_INTEGER t_now;
    QueryPerformanceCounter(&t_now);
   	double dt = (double)(t_now.QuadPart - timer->t_ref.QuadPart)/(double)timer->freq.QuadPart;
    return(dt);
} 


// write file with timeout with ACK per blocks
// note: this is workaround for pipe errors (pipe closure) which happens when trying to write more than some 40MBytes of data.
// I was not able to identify why it fails, so here the routine transfers blocks smaller than pipe buffer and waits for ACK
// of every single one. Not that fast, but still reasonably usable.
DWORD WriteFileTimeoutACK(HANDLE file, LPVOID data, DWORD size, DWORD *written_bytes, DWORD block_size, double total_timeout)
{
    TTimer timer;
    timer_init(&timer);    
    double timeout = total_timeout;
    char *pdata = (char*)data;
    DWORD written_total = 0;    
    
    // default no data written
    if(written_bytes)
        *written_bytes = 0;
    
    do{
    
        // write block size DWORD
        DWORD towr = min(size, block_size);
        if(WriteFileTimeout(file, &towr, sizeof(DWORD), NULL, block_size, timeout))
        {
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }     
        timeout = total_timeout - timer_get(&timer);
        if(timeout < 0.0)
        {
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }
        
        // write data block
        DWORD written = 0;
        if(WriteFileTimeout(file, (void*)pdata, towr, &written, block_size, timeout))
        {
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }
        written_total += written;
        pdata += written;
        size -= written;
        timeout = total_timeout - timer_get(&timer);
        if(timeout < 0.0)
        {
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }
            
        // wait for ACK
        char res;
        if(ReadFileTimeout(file, &res, 1, NULL, timeout))
        {
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }
        if(res != 'A')
        {
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }
        timeout = total_timeout - timer_get(&timer);
        
        if(timeout < 0)
        {
            // timeout
            if(written_bytes)
                *written_bytes = written_total;
            return(1);
        }
        
    }while(size > 0);
    
    // total written
    if(written_bytes)
        *written_bytes = written_total;
    
    return(0);
}

// write file with timeout	
DWORD WriteFileTimeout(HANDLE file, LPVOID data, DWORD size, DWORD *written_bytes, DWORD block_size, double timeout)
{
    // default no data written
    if(written_bytes)
        *written_bytes = 0;
             
    // empty data?
    if(!size)
        return(0);
        
    // write all at once if no write block size defined
    if(!block_size)
        block_size = size;
    
    // get initial timestamp 
    TTimer timer;
    timer_init(&timer);
    
    DWORD written_total = 0;
    DWORD to_write = size;  
    do{
    
        if(timer_get(&timer) >= timeout)
        {
            // total timeout
            if(DEBUG_PRN)
                octave_stdout << "write timeout\n";
            return(1);
        }
    
        // create async wait event
        HANDLE hEvent = CreateEvent( 
            NULL,    // default security attribute 
            true,    // manual-reset event 
            false,   // initial state 
            NULL);   // unnamed event object 
        if(!hEvent) 
            return(1);    
        OVERLAPPED overlap;
        overlap.Offset = 0;
        overlap.OffsetHigh = 0;
        overlap.hEvent = hEvent;
        
        // start async WriteFile()
        DWORD written = 0;
        char *p = (char*)data;
        if(!WriteFile(file, (void*)&p[written_total], min(to_write, block_size), NULL, &overlap))
        {                
            DWORD err = GetLastError();
            if(err != ERROR_IO_PENDING)
            {
                if(DEBUG_PRN)
                    octave_stdout << "write io not pending (err = " << err << ")\n";
                CloseHandle(hEvent);                
                return(1);
            }            
        }
        
        // wait with timeout
        DWORD err = WaitForSingleObject(hEvent, (DWORD)(timeout*1000.0));
        if(err == WAIT_TIMEOUT)
        {
            // timeout
            //CancelIoEx(file, &overlap);
            if(DEBUG_PRN)
                octave_stdout << "write timeout\n";
            CancelIo(file);
            CloseHandle(hEvent);
            return(1);
        }
            
        // get WriteFile() result
        DWORD fSuccess = GetOverlappedResult( 
            file, // handle to pipe 
            &overlap, // OVERLAPPED structure 
            &written,            // bytes transferred 
            false);            // do not wait
            
        written_total += written;
        to_write -= written;
                
        if(DEBUG_PRN)
            octave_stdout << "write wait return code " << fSuccess << ", written = " << written << ", total written = " << written_total << "\n";        
        
        // cleanup    
        CloseHandle(hEvent);
        
        
    
    }while(written_total < size);
    
    // return total read bytes
    if(written_bytes)
        *written_bytes = written_total;
    
    return(0);
}

// read file with timeout
DWORD ReadFileTimeout(HANDLE file, LPVOID data, DWORD size, DWORD *read_bytes, double timeout)
{    
    // default no data read
    if(read_bytes)
        *read_bytes = 0;
             
    // empty data?
    if(!size)
        return(0);
    
    // get initial timestamp 
    TTimer timer;
    timer_init(&timer);
    
    DWORD read_total = 0;
    DWORD to_read = size;  
    do{
    
        // create ReadFile() async wait event
        HANDLE hEvent = CreateEvent( 
            NULL,    // default security attribute 
            true,    // manual-reset event 
            false,   // initial state 
            NULL);   // unnamed event object 
        if(!hEvent) 
            return(1);    
        OVERLAPPED overlap;
        overlap.Offset = 0;
        overlap.OffsetHigh = 0;
        overlap.hEvent = hEvent;
        
        // start async ReadFile()
        DWORD read;
        char *p = (char*)data;
        if(!ReadFile(file, (void*)&p[read_total], to_read, NULL, &overlap))
        {                
            DWORD err = GetLastError();
            if(err != ERROR_IO_PENDING)
            {
                if(DEBUG_PRN)
                    octave_stdout << "read io not pending (err = " << err << ")\n";
                CloseHandle(hEvent);
                return(1);
            }
        }
        
        // wait with timeout
        DWORD err = WaitForSingleObject(hEvent, (DWORD)(timeout*1000.0));
        if(err == WAIT_TIMEOUT)
        {
            // timeout
            //CancelIoEx(file, &overlap);
            if(DEBUG_PRN)
                octave_stdout << "read timeout\n";
            CancelIo(file);
            CloseHandle(hEvent);
            return(1);
        }
            
        // get ReadFile() result
        DWORD fSuccess = GetOverlappedResult( 
            file, // handle to pipe 
            &overlap, // OVERLAPPED structure 
            &read,            // bytes transferred 
            false);            // do not wait
            
        read_total += read;
        to_read -= read;
                
        if(DEBUG_PRN)
            octave_stdout << "read wait return code " << err << ", read = " << read << ", total read = " << read_total << "\n";        
        
        // cleanup    
        CloseHandle(hEvent);
        
        if(timer_get(&timer) >= timeout)
        {
            // total timeout
            if(DEBUG_PRN)
                octave_stdout << "read timeout\n";
            return(1);
        }
    
    }while(read_total < size);
    
    // return total read bytes
    if(read_bytes)
        *read_bytes = read_total;
    
    return(0);
}



