program coupling
  use iso_c_binding, only: c_double, c_int64_t
  use mpi
  use cotocoa
  implicit none
  type(ctca_context) :: ctx
  type(ctca_message) :: message
  real(c_double) :: field(3,5,7)
  real(c_double), allocatable :: received(:,:,:), saved(:,:,:)
  integer :: rank, ierr, step, i, j, k, count, source, world_size, group_size, source_ranks, steps
  integer(c_int64_t) :: offset(3)
  logical :: available, borrowed, finalized
  character(len=1024) :: name, config, env
  call get_command_argument(1, name)
  call get_command_argument(2, config)
  call get_environment_variable('COTOCOA_TEST_BORROWED', env, status=ierr)
  borrowed = ierr == 0
  if (borrowed) call MPI_Init(ierr)
  steps = 100
  call get_environment_variable('COTOCOA_TEST_STEPS', env, status=ierr)
  if (ierr == 0) read(env,*) steps
  call ctx%open(trim(name), config=trim(config))
  call MPI_Comm_rank(ctx%comm(), rank, ierr)
  if (trim(name) == 'producer') then
    do step = 1, steps
      do k=1,7
        do j=1,5
          do i=1,3
            field(i,j,k) = rank*1000000 + step*1000 + (i-1)*100 + (j-1)*10 + k-1
          end do
        end do
      end do
      offset = [int(rank*3,c_int64_t), 0_c_int64_t, 0_c_int64_t]
      call ctx%send('field', field, step=int(step,c_int64_t), &
                    time=step*0.01_c_double, offset=offset)
      field = 0.0_c_double
    end do
    call ctx%flush()
  else
    call MPI_Comm_size(MPI_COMM_WORLD, world_size, ierr)
    call MPI_Comm_size(ctx%comm(), group_size, ierr)
    source_ranks = world_size-group_size-1
    count = 0
    do
      call ctx%receive('field', message, has_message=available)
      if (.not. available) exit
      call message%copy_to(received)
      source = message%source_rank()
      call require(message%sequence() == message%step(), 'sequence')
      call require(all(message%offset() == [int(source*3,c_int64_t),0_c_int64_t,0_c_int64_t]), 'offset')
      call require(all(shape(received) == [3,5,7]), 'shape')
      call require(all(message%shape() == [3_c_int64_t,5_c_int64_t,7_c_int64_t]), 'message shape')
      call require(message%has_time() .and. message%time() == message%step()*0.01_c_double, 'time')
      call require(message%units() == 'V' .and. message%time_unit() == 's', 'units')
      call require(all(message%axes() == ['x','y','z']), 'axes')
      do k=1,7
        do j=1,5
          do i=1,3
            call require(received(i,j,k) == source*1000000 + message%step()*1000 + &
                         (i-1)*100+(j-1)*10+k-1, 'snapshot/axis order')
          end do
        end do
      end do
      if (.not. allocated(saved)) saved = received
      call message%complete()
      count = count+1
    end do
    call require(count == steps*source_ranks, 'all delivery count')
    if (allocated(saved)) call require(saved(1,1,1) >= 1000, 'retained snapshot')
    print *, 'PASS Fortran rank=',rank,' messages=',count
  end if
  call ctx%close()
  call MPI_Finalized(finalized, ierr)
  call require(finalized .neqv. borrowed, 'MPI ownership')
  if (borrowed) then
    call MPI_Barrier(MPI_COMM_SELF, ierr)
    call MPI_Finalize(ierr)
  end if
contains
  subroutine require(condition, text)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: text
    if (.not. condition) then
      print *, 'FAIL Fortran: ', text
      call MPI_Abort(MPI_COMM_WORLD, 1, ierr)
    end if
  end subroutine
end program coupling
