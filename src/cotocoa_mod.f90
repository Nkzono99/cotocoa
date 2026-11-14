module cotocoa
  use, intrinsic :: iso_c_binding
  implicit none
  private
  public :: ctca_context, ctca_message, ctca_status
  integer, parameter, public :: COTOCOA_FLOAT64=1, COTOCOA_FLOAT32=2, COTOCOA_INT32=3

  type, bind(c) :: c_error
    integer(c_int) :: code
    character(c_char) :: message(512)
  end type

  type, bind(c) :: c_view
    type(c_ptr) :: data
    integer(c_int) :: dtype, ndim
    integer(c_int64_t) :: shape(3), strides(3)
  end type

  type, bind(c) :: c_options
    integer(c_int64_t) :: step
    integer(c_int) :: has_time
    real(c_double) :: time
    integer(c_int64_t) :: offset(3)
  end type

  type, bind(c) :: c_metadata
    integer(c_int64_t) :: step, sequence
    integer(c_int) :: source_rank, has_time
    real(c_double) :: time
    integer(c_int64_t) :: shape(3), global_shape(3), offset(3)
    character(c_char) :: axes(64, 3), units(64), time_unit(64)
    integer(c_int) :: dtype, ndim
  end type

  type :: ctca_status
    integer :: code = 0
    character(len=512) :: message = ''
  end type

  type :: ctca_context
    private
    type(c_ptr) :: handle = c_null_ptr
  contains
    procedure :: open => context_open
    procedure :: close => context_close
    procedure :: comm => context_comm
    procedure, private :: send_real64_1, send_real64_2, send_real64_3
    procedure, private :: send_real32_1, send_real32_2, send_real32_3
    procedure, private :: send_int32_1, send_int32_2, send_int32_3
    generic :: send => send_real64_1, send_real64_2, send_real64_3, &
                      send_real32_1, send_real32_2, send_real32_3, &
                      send_int32_1, send_int32_2, send_int32_3
    procedure :: receive => context_receive
    procedure :: flush => context_flush
  end type

  type :: ctca_message
    private
    type(c_ptr) :: handle = c_null_ptr
    type(c_metadata) :: meta
  contains
    procedure, private :: copy_real64_1, copy_real64_2, copy_real64_3
    procedure, private :: copy_real32_1, copy_real32_2, copy_real32_3
    procedure, private :: copy_int32_1, copy_int32_2, copy_int32_3
    generic :: copy_to => copy_real64_1, copy_real64_2, copy_real64_3, &
                         copy_real32_1, copy_real32_2, copy_real32_3, &
                         copy_int32_1, copy_int32_2, copy_int32_3
    procedure :: complete => message_complete
    procedure :: step => message_step
    procedure :: sequence => message_sequence
    procedure :: source_rank => message_source_rank
    procedure :: offset => message_offset
    procedure :: global_shape => message_global_shape
    procedure :: shape => message_shape
    procedure :: has_time => message_has_time
    procedure :: time => message_time
    procedure :: axes => message_axes
    procedure :: units => message_units
    procedure :: time_unit => message_time_unit
    procedure :: dtype => message_dtype
    procedure :: ndim => message_ndim
  end type

  interface
    function c_open(name, config, ctx, error) bind(c, name='cotocoa_open') result(rc)
      import
      character(c_char), intent(in) :: name(*), config(*)
      type(c_ptr), intent(out) :: ctx
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    function c_close(ctx, error) bind(c, name='cotocoa_close') result(rc)
      import
      type(c_ptr), value :: ctx
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    function c_comm(ctx) bind(c, name='cotocoa_comm_f') result(comm)
      import
      type(c_ptr), value :: ctx
      integer(c_int) :: comm
    end function
    subroutine c_abort(ctx, error) bind(c, name='cotocoa_abort')
      import
      type(c_ptr), value :: ctx
      type(c_error), intent(in) :: error
    end subroutine
    function c_make_view(data, ndim, shape, order) bind(c, name='cotocoa_view_f64') result(view)
      import
      type(c_ptr), value :: data
      integer(c_int), value :: ndim, order
      integer(c_int64_t), intent(in) :: shape(3)
      type(c_view) :: view
    end function
    function c_send(ctx, name, view, options, error) bind(c, name='cotocoa_send') result(rc)
      import
      type(c_ptr), value :: ctx
      character(c_char), intent(in) :: name(*)
      type(c_view), intent(in) :: view
      type(c_options), intent(in) :: options
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    function c_make_view_f32(data, ndim, shape, order) bind(c, name='cotocoa_view_f32') result(view)
      import
      type(c_ptr), value :: data
      integer(c_int), value :: ndim, order
      integer(c_int64_t), intent(in) :: shape(3)
      type(c_view) :: view
    end function
    function c_make_view_i32(data, ndim, shape, order) bind(c, name='cotocoa_view_i32') result(view)
      import
      type(c_ptr), value :: data
      integer(c_int), value :: ndim, order
      integer(c_int64_t), intent(in) :: shape(3)
      type(c_view) :: view
    end function
    function c_receive(ctx, name, message, error) bind(c, name='cotocoa_receive') result(rc)
      import
      type(c_ptr), value :: ctx
      character(c_char), intent(in) :: name(*)
      type(c_ptr), intent(out) :: message
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    function c_get_view(message, view, error) bind(c, name='cotocoa_message_view') result(rc)
      import
      type(c_ptr), value :: message
      type(c_view), intent(out) :: view
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    function c_get_metadata(message, metadata, error) bind(c, name='cotocoa_message_metadata') result(rc)
      import
      type(c_ptr), value :: message
      type(c_metadata), intent(out) :: metadata
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    function c_complete(message, error) bind(c, name='cotocoa_complete') result(rc)
      import
      type(c_ptr), value :: message
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
    subroutine c_release(message) bind(c, name='cotocoa_message_release')
      import
      type(c_ptr), value :: message
    end subroutine
    function c_flush(ctx, error) bind(c, name='cotocoa_flush') result(rc)
      import
      type(c_ptr), value :: ctx
      type(c_error), intent(out) :: error
      integer(c_int) :: rc
    end function
  end interface

contains

  subroutine report(rc, error, ctx, status)
    integer(c_int), intent(in) :: rc
    type(c_error), intent(in) :: error
    type(c_ptr), intent(in) :: ctx
    type(ctca_status), optional, intent(out) :: status
    integer :: i
    if (present(status)) then
      status%code = rc
      status%message = ''
      if (rc < 0) then
        do i = 1, size(error%message)
          if (error%message(i) == c_null_char) exit
          status%message(i:i) = error%message(i)
        end do
      end if
    else if (rc < 0) then
      call c_abort(ctx, error)
    end if
  end subroutine

  subroutine local_error(error, text)
    type(c_error), intent(out) :: error
    character(len=*), intent(in) :: text
    integer :: i
    error%code = -2
    error%message = c_null_char
    do i = 1, min(len_trim(text), 511)
      error%message(i) = text(i:i)
    end do
  end subroutine

  subroutine context_open(self, name, config, status)
    class(ctca_context), intent(inout) :: self
    character(len=*), intent(in) :: name
    character(len=*), optional, intent(in) :: config
    type(ctca_status), optional, intent(out) :: status
    type(c_error) :: error
    integer(c_int) :: rc
    character(len=:), allocatable :: path
    path = 'cotocoa.yaml'
    if (present(config)) path = config
    if (c_associated(self%handle)) then
      call local_error(error, 'Context is already open')
      rc = -2
    else
      rc = c_open(trim(name)//c_null_char, trim(path)//c_null_char, self%handle, error)
    end if
    call report(rc, error, self%handle, status)
  end subroutine

  subroutine context_close(self, status)
    class(ctca_context), intent(inout) :: self
    type(ctca_status), optional, intent(out) :: status
    type(c_error) :: error
    integer(c_int) :: rc
    rc = c_close(self%handle, error)
    if (rc == 0) self%handle = c_null_ptr
    call report(rc, error, self%handle, status)
  end subroutine

  function context_comm(self) result(comm)
    class(ctca_context), intent(in) :: self
    integer :: comm
    comm = c_comm(self%handle)
  end function

  include 'cotocoa_arrays.inc'

  subroutine context_receive(self, name, message, has_message, status)
    class(ctca_context), intent(inout) :: self
    character(len=*), intent(in) :: name
    type(ctca_message), intent(inout) :: message
    logical, intent(out) :: has_message
    type(ctca_status), optional, intent(out) :: status
    type(c_error) :: error
    integer(c_int) :: rc
    has_message = .false.
    if (c_associated(message%handle)) then
      call local_error(error, 'previous message must be completed before receive')
      call c_abort(self%handle, error)
    end if
    rc = c_receive(self%handle, trim(name)//c_null_char, message%handle, error)
    if (rc == 0) then
      rc = c_get_metadata(message%handle, message%meta, error)
      has_message = rc == 0
    end if
    call report(rc, error, self%handle, status)
  end subroutine

  subroutine context_flush(self, status)
    class(ctca_context), intent(inout) :: self
    type(ctca_status), optional, intent(out) :: status
    type(c_error) :: error
    integer(c_int) :: rc
    rc = c_flush(self%handle, error)
    call report(rc, error, self%handle, status)
  end subroutine


  subroutine message_complete(self, status)
    class(ctca_message), intent(inout) :: self
    type(ctca_status), optional, intent(out) :: status
    type(c_error) :: error
    integer(c_int) :: rc
    rc = c_complete(self%handle, error)
    if (rc == 0) then
      call c_release(self%handle)
      self%handle = c_null_ptr
    end if
    call report(rc, error, c_null_ptr, status)
  end subroutine

  function message_step(self) result(value)
    class(ctca_message), intent(in) :: self
    integer(c_int64_t) :: value
    value = self%meta%step
  end function

  function message_sequence(self) result(value)
    class(ctca_message), intent(in) :: self
    integer(c_int64_t) :: value
    value = self%meta%sequence
  end function

  function message_source_rank(self) result(value)
    class(ctca_message), intent(in) :: self
    integer :: value
    value = self%meta%source_rank
  end function

  function message_offset(self) result(value)
    class(ctca_message), intent(in) :: self
    integer(c_int64_t), allocatable :: value(:)
    value = self%meta%offset(:self%meta%ndim)
  end function

  function message_global_shape(self) result(value)
    class(ctca_message), intent(in) :: self
    integer(c_int64_t), allocatable :: value(:)
    value = self%meta%global_shape(:self%meta%ndim)
  end function

  function message_shape(self) result(value)
    class(ctca_message), intent(in) :: self
    integer(c_int64_t), allocatable :: value(:)
    value = self%meta%shape(:self%meta%ndim)
  end function

  function message_has_time(self) result(value)
    class(ctca_message), intent(in) :: self
    logical :: value
    value = self%meta%has_time /= 0
  end function

  function message_time(self) result(value)
    class(ctca_message), intent(in) :: self
    real(c_double) :: value
    value = self%meta%time
  end function

  function label(text) result(value)
    character(kind=c_char), intent(in) :: text(64)
    character(len=64) :: value
    integer :: i
    value = ''
    do i = 1, 64
      if (text(i) == c_null_char) exit
      value(i:i) = text(i)
    end do
  end function

  function message_axes(self) result(value)
    class(ctca_message), intent(in) :: self
    character(len=64), allocatable :: value(:)
    integer :: i
    allocate(value(self%meta%ndim))
    do i = 1, self%meta%ndim
      value(i) = label(self%meta%axes(:,i))
    end do
  end function

  function message_units(self) result(value)
    class(ctca_message), intent(in) :: self
    character(len=64) :: value
    value = label(self%meta%units)
  end function

  function message_time_unit(self) result(value)
    class(ctca_message), intent(in) :: self
    character(len=64) :: value
    value = label(self%meta%time_unit)
  end function

  function message_dtype(self) result(value)
    class(ctca_message), intent(in) :: self
    integer :: value
    value = self%meta%dtype
  end function

  function message_ndim(self) result(value)
    class(ctca_message), intent(in) :: self
    integer :: value
    value = self%meta%ndim
  end function
end module cotocoa
