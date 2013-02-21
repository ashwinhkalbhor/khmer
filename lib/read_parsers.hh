#ifndef READ_PARSERS_HH
#define READ_PARSERS_HH


#include <cassert>
#include <cstdarg>

#include <string>

extern "C"
{
#include <regex.h>
}

#include "zlib/zlib.h"
#include "bzip2/bzlib.h"

#include "khmer.hh"
#include "khmer_config.hh"
#include "thread_id_map.hh"
#include "trace_logger.hh"
#include "perf_metrics.hh"


namespace khmer
{


struct InvalidStreamHandle : public std:: exception
{ };

struct StreamReadError : public std:: exception
{ };

struct NoMoreReadsAvailable : public std:: exception
{ };


namespace read_parsers
{


struct InvalidReadFileFormat: public std:: exception
{ };

struct InvalidFASTAFileFormat: public InvalidReadFileFormat
{ };

struct InvalidFASTQFileFormat: public InvalidReadFileFormat
{ };

struct CacheSegmentUnavailable : public std:: exception
{ };

struct CacheSegmentBoundaryViolation : public std:: exception
{ };

struct InvalidCacheSizeRequested : public std:: exception
{ };


struct StreamReaderPerformanceMetrics : public IPerformanceMetrics
{
    
    enum
    {
	MKEY_TIME_READING
    };
    
    uint64_t	    numbytes_read;
    uint64_t	    clock_nsecs_reading;
    uint64_t	    cpu_nsecs_reading;
    
	    StreamReaderPerformanceMetrics( );
    virtual ~StreamReaderPerformanceMetrics( );

    virtual void    accumulate_timer_deltas( uint32_t metrics_key );

};


struct IStreamReader
{

    StreamReaderPerformanceMetrics  pmetrics;
    
	    IStreamReader( );
    virtual ~IStreamReader( );

    size_t const		    get_memory_alignment( ) const;

    bool const			    is_at_end_of_stream( ) const;

    virtual uint64_t const	    read_into_cache(
	uint8_t * const cache, uint64_t const cache_size
    ) = 0;

protected:

    size_t			    _alignment;
    size_t			    _max_aligned;
    
    bool			    _at_eos;

};


struct RawStreamReader : public IStreamReader
{
    
	    RawStreamReader( int const fd, size_t const alignment = 0 );
    virtual ~RawStreamReader( );

    virtual uint64_t const  read_into_cache(
	uint8_t * const cache, uint64_t const cache_size
    );

protected:
    
    int			    _stream_handle;

};


struct GzStreamReader : public IStreamReader
{
    
	    GzStreamReader( int const fd );
    virtual ~GzStreamReader( );

    virtual uint64_t const  read_into_cache(
	uint8_t * const cache, uint64_t const cache_size
    );

private:
    
    gzFile		    _stream_handle;

};


struct Bz2StreamReader : public IStreamReader
{
    
	    Bz2StreamReader( int const fd );
    virtual ~Bz2StreamReader( );

    virtual uint64_t const  read_into_cache(
	uint8_t * const cache, uint64_t const cache_size
    );

private:
    
    FILE *		    _stream_handle;
    BZFILE *		    _block_handle;

};


struct CacheSegmentPerformanceMetrics : public IPerformanceMetrics
{
    
    enum
    {
	MKEY_TIME_WAITING_TO_SET_SA_BUFFER,
	MKEY_TIME_WAITING_TO_GET_SA_BUFFER,
	MKEY_TIME_WAITING_TO_FILL_FROM_STREAM,
	MKEY_TIME_FILLING_FROM_STREAM,
	MKEY_TIME_IN_SYNC_BARRIER
    };
    
    uint64_t	    numbytes_filled_from_stream;
    uint64_t	    numbytes_copied_from_sa_buffer;
    uint64_t	    numbytes_reserved_as_sa_buffer;
    uint64_t	    numbytes_copied_to_caller_buffer;
    uint64_t	    clock_nsecs_waiting_to_set_sa_buffer;
    uint64_t	    cpu_nsecs_waiting_to_set_sa_buffer;
    uint64_t	    clock_nsecs_waiting_to_get_sa_buffer;
    uint64_t	    cpu_nsecs_waiting_to_get_sa_buffer;
    uint64_t	    clock_nsecs_waiting_to_fill_from_stream;
    uint64_t	    cpu_nsecs_waiting_to_fill_from_stream;
    uint64_t	    clock_nsecs_filling_from_stream;
    uint64_t	    cpu_nsecs_filling_from_stream;
    uint64_t	    clock_nsecs_in_sync_barrier;
    uint64_t	    cpu_nsecs_in_sync_barrier;

	    CacheSegmentPerformanceMetrics( );
    virtual ~CacheSegmentPerformanceMetrics( );

    virtual void    accumulate_timer_deltas( uint32_t metrics_key );

    virtual void    accumulate_metrics(
	CacheSegmentPerformanceMetrics &source
    );

protected:
    
    uint32_t	    _accumulated_count;

};


struct CacheManager
{
    
    CacheManager(
	IStreamReader &	stream_reader,
	uint32_t const	number_of_threads,
	uint64_t const	cache_size,
	uint8_t const	trace_level =
	khmer:: get_active_config( ).get_input_buffer_trace_level( )
    );
    ~CacheManager( );

    // Returns true, if current thread has more bytes to consume.
    // Blocks, if current thread has no more bytes to consume, 
    //   but other threads still do. (Synchronization barrier.)
    // Returns false, if no threads have more bytes to consume.
    bool const		has_more_data( );

    uint64_t const	get_bytes(
	uint8_t * const buffer, uint64_t buffer_len
    );

    uint64_t const	whereis_cursor( void );
    bool const		is_cursor_in_ca_buffer( void );
    void		split_at( uint64_t const pos );

    uint64_t const	get_fill_id( );

private:
    
    struct CacheSegment
    {

	bool				avail;
	uint32_t			thread_id;
	uint64_t			size;
	size_t				alignment;
	uint8_t *			memory;
	uint64_t			cursor;
	bool				cursor_in_ca_buffer;
	std:: string			ca_buffer;
	uint64_t			fill_id;
	CacheSegmentPerformanceMetrics	pmetrics;
	TraceLogger			trace_logger;
	
	CacheSegment(
	    uint32_t const  thread_id,
	    uint64_t const  size,
	    size_t const    alignment = 0,
	    uint8_t const   trace_level = 
	    khmer:: get_active_config( ).get_input_buffer_trace_level( )
	);
	~CacheSegment( );

    }; // struct CacheSegment

    uint8_t		_trace_level;

    IStreamReader &	_stream_reader;

    uint32_t		_number_of_threads;
    ThreadIDMap		_thread_id_map;

    size_t		_alignment;
    uint64_t		_segment_size;
    CacheSegment **	_segments;
    uint32_t		_segment_ref_count;
    uint32_t		_segment_to_fill;
    uint64_t		_fill_counter;

    // Copyaside buffers.
    std:: map< uint64_t, std:: string >	_ca_buffers;
    uint32_t				_ca_spin_lock;

    // Extends or refills segment for current thread, as needed.
    void		_perform_segment_maintenance(
	CacheSegment & segment
    );

    bool const		_check_segment_to_fill_ATOMIC(
	uint32_t const thread_id
    );
    void		_select_segment_to_fill_ATOMIC( );
    CacheSegment &	_get_segment( bool const higher = false );
    void		_fill_segment_from_stream(
	CacheSegment & segment
    );
    void		_increment_segment_ref_count_ATOMIC( );
    void		_decrement_segment_ref_count_ATOMIC( );
    uint32_t const	_get_segment_ref_count_ATOMIC( );
    
}; // struct CacheManager


struct Read
{
    std:: string    name;
    std:: string    annotations;
    std:: string    sequence;
    std:: string    accuracy;
    // TODO? Add description field.
    uint64_t	    bytes_consumed;

    inline void reset ( )
    {
	name.clear( );
	annotations.clear( );
	sequence.clear( );
	accuracy.clear( );
	bytes_consumed = 0;
    }
};


struct ParserPerformanceMetrics: public IPerformanceMetrics
{
    
    uint64_t	    numlines_copied;
    uint64_t	    numreads_parsed_total;
    uint64_t	    numreads_parsed_valid;

	    ParserPerformanceMetrics( );
    virtual ~ParserPerformanceMetrics( );

    virtual void    accumulate_timer_deltas( uint32_t metrics_key );

};


struct IParser
{

    static IParser * const  get_parser(
	std:: string const 	&ifile_name,
	uint32_t const		number_of_threads   =
	khmer:: get_active_config( ).get_number_of_threads( ),
	uint64_t const		cache_size	    =
	khmer:: get_active_config( ).get_reads_input_buffer_size( ),
	uint8_t const		trace_level	    =
	khmer:: get_active_config( ).get_reads_parser_trace_level( )
    );
    
	    IParser(
	IStreamReader	&stream_reader,
	uint32_t const	number_of_threads   =
	khmer:: get_active_config( ).get_number_of_threads( ),
	uint64_t const	cache_size	    =
	khmer:: get_active_config( ).get_reads_input_buffer_size( ),
	uint8_t const	trace_level	    = 
	khmer:: get_active_config( ).get_reads_parser_trace_level( )
    );
    virtual ~IParser( );

    inline bool		is_complete( )
    { return !_cache_manager.has_more_data( ) && !_get_state( ).buffer_rem; }

    virtual Read	get_next_read( );

protected:
    
    struct ParserState
    {

	// TODO: Set buffer size from Config.
	static uint64_t const	    BUFFER_SIZE		= 127;

	uint32_t		    thread_id;
	
	bool			    at_start;
	uint64_t		    fill_id;

	std:: string		    line;
	bool			    need_new_line;

	uint8_t			    buffer[ BUFFER_SIZE + 1 ];
	uint64_t		    buffer_pos;
	uint64_t		    buffer_rem;

	regex_t			    re_read_2;

	ParserPerformanceMetrics    pmetrics;
	TraceLogger		    trace_logger;
	
	ParserState( uint32_t const thread_id, uint8_t const trace_level );
	~ParserState( );

    }; // struct ParserState
    
    uint8_t		_trace_level;

    CacheManager	_cache_manager;

    uint32_t		_number_of_threads;
    ThreadIDMap		_thread_id_map;
    bool		_unithreaded;

    ParserState **	_states;

    void		_copy_line( ParserState &state );

    virtual void	_parse_read( ParserState &, Read &)	    = 0;

    inline ParserState	&_get_state( )
    {
	uint32_t	thread_id	= _thread_id_map.get_thread_id( );
	ParserState *	state_PTR	= NULL;

	assert( NULL != _states );

	state_PTR = _states[ thread_id ];
	if (NULL == state_PTR)
	{
	    _states[ thread_id ]    =
	    new ParserState( thread_id, _trace_level );
	    state_PTR		    = _states[ thread_id ];
	}

	return *state_PTR;
    }

}; // struct IParser


struct FastaParser : public IParser
{
    
	    FastaParser(
	IStreamReader &	stream_reader,
	uint32_t const	number_of_threads,
	uint64_t const	cache_size,
	uint8_t const	trace_level
    );
    virtual ~FastaParser( );

    virtual void    _parse_read( ParserState &, Read &);

};


struct FastqParser : public IParser
{

	    FastqParser(
	IStreamReader &	stream_reader,
	uint32_t const	number_of_threads,
	uint64_t const	cache_size,
	uint8_t const	trace_level
    );
    virtual ~FastqParser( );

    virtual void    _parse_read( ParserState &, Read &);

};


} // namespace read_parsers


} // namespace khmer


#endif // READ_PARSERS_HH

// vim: set ft=cpp sts=4 sw=4 tw=80: