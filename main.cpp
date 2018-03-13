#include <stdlib.h>
#include <map>
#include <deque>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

using namespace std;
/* We want the extra information from these definitions */
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <ucontext.h>


#define SECOND 1000000

#define STACK_SIZE 20000

typedef unsigned long address_t;

typedef enum Thread_State {ready, running, suspended, terminated}Thread_State;

class TCB {
  ucontext_t *thread_context;
  int thread_id;
  address_t thread_stack_pointer;
  Thread_State thread_state;

  //Function entry point and argument
  void *return_value;
  void *argument;//To be done
      //Remove from running state and schedule a new thread
      //Notify the the thread that is waiting
      //*Decide how to change the curent running thread variable
  bool is_main_thread_waiting;

public :
  TCB(int tid,ucontext_t *thread_context,address_t sp, void *arg)
	{
		this->thread_id=tid;
		this->thread_stack_pointer=sp;
		this->thread_state=ready;
		this->thread_context = thread_context;
		this->argument = arg;
		this->return_value = NULL;
		this->is_main_thread_waiting = false;
	}
  Thread_State get_thread_state() {
    return this->thread_state;
  }

  void set_main_thread_waiting()
  {
    this->is_main_thread_waiting=true;
  }

  void set_thread_state(Thread_State thread_state) {
    this->thread_state=thread_state;
  }

  ucontext_t *get_saved_context() {
    return this->thread_context;
  }

  void save_context(ucontext_t *context) {
    this->thread_context = context;
  }

  int get_tid(){
    return this->thread_id;
  }

  void set_return_value(void *return_value) {
    this->return_value = return_value;
  }

  void *get_return_value() {
    return this->return_value;
  }

};

//Function declarations
void timer_signal_handler (int signum);
void stub(void *(* start_routine)(void *), void *arg);
int uthread_run();
int uthread_yield();
int uthread_self();
void set_currently_running_tid(int thread_id);

//Global variables
int tid = -1;
map<int,TCB *> *global_thread_directory=NULL;
deque<int> *ready_queue=NULL;
deque<int> *suspended_queue=NULL;
int currently_running_tid = -1;
int main_thread_tid = -1;
int quantum = -1;
sigset_t vtalrm;
struct sigaction sa;
struct itimerval timer;

int uthread_init(int time_slice) {
  //Thread ids will start from 0
  tid = 0;
  global_thread_directory = new map<int, TCB *>();
  ready_queue = new deque<int>();
  suspended_queue = new deque<int>();
  quantum = time_slice;

  /* setting uo the signal mask */
  sigemptyset(&vtalrm);
  sigaddset(&vtalrm, SIGVTALRM);
  /* in case this is blocked previously */
  #warning unblocking timer signal
  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);

  /* Install timer_handler as the signal handler for SIGVTALRM. */
  memset (&sa, 0, sizeof (sa));
  sa.sa_handler = &timer_signal_handler;
  sigaction (SIGVTALRM, &sa, NULL);

  /* Configure the timer to expire after 250 msec... */
  timer.it_value.tv_sec = 2;
  timer.it_value.tv_usec = 0;
  /* ... and every 250 msec after that. */
  timer.it_interval.tv_sec = 2;
  timer.it_interval.tv_usec = 0;
  /* Start a virtual timer. It counts down whenever this process is executing. */
  // sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  //setitimer (ITIMER_VIRTUAL, &timer, NULL);

  return 0;
}

bool uthread_initialised() {
  if(tid == -1 || global_thread_directory == NULL || ready_queue == NULL || suspended_queue == NULL || quantum == -1) {
    return false;
  }
  return true;
}

int uthread_create(void *(* start_routine)(void *), void *arg) {
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  address_t sp, pc;

  ucontext_t *thread_context = (ucontext_t *)malloc(sizeof(ucontext_t));
  char *stack = (char *)malloc(sizeof(STACK_SIZE));
  sp=(address_t)stack + STACK_SIZE - sizeof(int);
  #warning Add stub address not the thread function address
  pc=(address_t)stub;

  //Return if there's any problem in getting a current context
  if(getcontext(thread_context)==-1) {
    return -1;
  }

  /* First argument to be passed in RDI register */
  thread_context->uc_mcontext.gregs[REG_RIP] = pc;
  thread_context->uc_mcontext.gregs[REG_RSP] = sp;
  thread_context->uc_mcontext.gregs[REG_RDI] = (greg_t)start_routine;
  thread_context->uc_mcontext.gregs[REG_RSI] = (greg_t)arg;
  sigemptyset(&thread_context->uc_sigmask);

  //Create TCB
  TCB *thread =  new TCB(tid, thread_context, sp, arg);

  //Add it to the global thread directory.
  if(uthread_initialised()) {
    global_thread_directory->insert(make_pair(tid, thread));
  }
  else {
    return -1;
  }

  //Adding the thread to the ready Q
  ready_queue->push_back(tid);
  tid++;

  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  return 0;
}

int uthread_terminate(int thread_id) {
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  TCB *thread = global_thread_directory->operator[](thread_id);
  if(thread!=NULL) {
    Thread_State state= thread->get_thread_state();
    switch(state) {
    case ready:
      for(deque<int>::iterator i=ready_queue->begin(); i!=ready_queue->end(); i++) {
	if(*i == thread_id) {
	  ready_queue->erase(i);
	  thread->set_thread_state(terminated);
	  break;
	}
      }
      break;
    case running:
      thread->set_thread_state(terminated);
      uthread_run();
      #warning if main thread is waiting
      #warning if the thread is the last one
      break;
    case suspended:
       for(deque<int>::iterator i=suspended_queue->begin(); i!=suspended_queue->end(); i++) {
	 if(*i == thread_id) {
	   suspended_queue->erase(i);
	   thread->set_thread_state(terminated);
	   break;
	 }
       }
      break;
    case terminated:
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
      return -1;
    default:
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
      return -1;
    }
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return 0;
  }
  else {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
  }

}

int uthread_suspend(int thread_id) {
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  TCB *thread = global_thread_directory->operator[](thread_id);
  if(thread!=NULL) {
     Thread_State state= thread->get_thread_state();
     switch(state) {
     case ready:
       for(deque<int>::iterator i=ready_queue->begin(); i!=ready_queue->end(); i++) {
	 if(*i == thread_id) {
	   ready_queue->erase(i);
	   thread->set_thread_state(suspended);
	   suspended_queue->push_back(thread_id);
	   break;
	 }
       }
       break;
     case running:
       //To be aked
       //Can we call a suspend on a thread which is already running?
       //If Yes, then who will call it and when?
       //*Decide how to change the curent running thread variable
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     case suspended:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     case terminated:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     default:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     }
     sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
     return 0;
  }
  else {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
  }
}

int uthread_resume(int thread_id) {
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  TCB *thread = global_thread_directory->operator[](thread_id);
  if(thread!=NULL) {
     Thread_State state= thread->get_thread_state();
     switch(state) {
     case ready:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     case running:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     case suspended:
       for(deque<int>::iterator i=suspended_queue->begin(); i!=suspended_queue->end(); i++) {
	 if(*i == thread_id) {
	   suspended_queue->erase(i);
	   thread->set_thread_state(ready);
	   ready_queue->push_back(thread_id);
	   break;
	 }
       }
       break;
     case terminated:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     default:
       sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return -1;
     }
     sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
     return 0;
  }
  else {
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
  }
}

int switch_threads() {
    sigprocmask(SIG_BLOCK, &vtalrm, NULL);
    int thread_id=uthread_self();
    TCB *current_tcb=global_thread_directory->operator[](thread_id);
    ucontext_t *thread_context = current_tcb->get_saved_context();
    volatile int flag = 0;
    if(getcontext(thread_context)==-1) {
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
      return -1;
    }
    if(flag == 1) {
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
       return 0;
    }
    flag = 1;
    if(!ready_queue->empty()) {
      int next_tid = ready_queue->front();
      TCB *next_TCB = global_thread_directory->operator[](next_tid);
      if(next_TCB == NULL) {
	sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
	return -1;
      }

      current_tcb->set_thread_state(ready);

      current_tcb->save_context(thread_context);

      ready_queue->push_back(current_tcb->get_tid());
      ready_queue->pop_front();
      set_currently_running_tid(next_tid);
      next_TCB->set_thread_state(running);
      ucontext_t *next_context = next_TCB->get_saved_context();
      sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
      setcontext(next_context);
    }
    sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
    return -1;
}

int uthread_join(int thread_tid, void **retval) {

  //Main thread can't join on itself
  if(uthread_self() == thread_tid) {
    return -1;
  }

  TCB *thread_tcb = global_thread_directory->operator[](thread_tid);
  if(thread_tcb == NULL) {
    return -1;
  }

  thread_tcb->set_main_thread_waiting();

  while(thread_tcb->get_thread_state()!=terminated) {
    #warning Add signal unblock and block
    #warning preemption later
    if(uthread_yield()==-1) {
      cout<<endl<<"Error in yielding"<<endl;
    }
  }

  *retval = thread_tcb->get_return_value();
  return 0;

}

int uthread_yield() {
  if(switch_threads()==-1) {
    cout<<"Error in switching threads"<<endl;
  }
}

void timer_signal_handler (int signum) {
  sigprocmask(SIG_BLOCK,&vtalrm,NULL);
  cout<<endl<<"Thread "<<uthread_self()<<" is going to switch."<<endl;
  if(switch_threads()==-1) {
    cout<<"Error in switching threads"<<endl;
  }
  cout<<endl<<"Thread "<<uthread_self()<<" awakened."<<endl;
  sigprocmask(SIG_UNBLOCK,&vtalrm,NULL);
}

int uthread_self() {
  //Only a calling thread can call self on itself.
  return currently_running_tid;
}

void set_currently_running_tid(int thread_id) {
  currently_running_tid = thread_id;
}

int main_thread_setup() {
  //Create a tb for main thread and dont add it to the ready queue
  sigprocmask(SIG_BLOCK, &vtalrm, NULL);
  address_t sp, pc;

  ucontext_t *thread_context = (ucontext_t *)malloc(sizeof(ucontext_t));
#warning No need to allocate stack and program counter
  sp = 0;
  pc = 0;

  //Return if there's any problem in getting a current context
  if(getcontext(thread_context)==-1) {
    return -1;
  }

  /* First argument to be passed in RDI register */
  thread_context->uc_mcontext.gregs[REG_RIP] = pc;
  thread_context->uc_mcontext.gregs[REG_RSP] = sp;
  thread_context->uc_mcontext.gregs[REG_RDI] = NULL;
  thread_context->uc_mcontext.gregs[REG_RSI] = NULL;
  sigemptyset(&thread_context->uc_sigmask);

  //Create TCB
  TCB *thread =  new TCB(tid, thread_context, sp, NULL);
  main_thread_tid = tid;

  //Add it to the global thread directory.
  if(uthread_initialised()) {
    global_thread_directory->insert(make_pair(tid, thread));
  }
  else {
    return -1;
  }

  //Adding the thread to the ready Q
#warning No need to add main thread in ready queue
  tid++;

  //Setup state running for main thread
  set_currently_running_tid(main_thread_tid);
  thread->set_thread_state(running);

  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
  return 0;
}

int uthread_run(){
      if(!ready_queue->empty()) {
	sigprocmask(SIG_BLOCK,&vtalrm,NULL);
	int next_tid = ready_queue->front();
	TCB *next_TCB = global_thread_directory->operator[](next_tid);
	if(next_TCB == NULL) {
	  sigprocmask(SIG_UNBLOCK, &vtalrm, NULL);
	  return -1;
	}
	next_TCB->set_thread_state(running);
	ready_queue->pop_front();
	set_currently_running_tid(next_tid);
	ucontext_t *next_context = next_TCB->get_saved_context();
	sigprocmask(SIG_UNBLOCK,&vtalrm,NULL);
	setcontext(next_context);
	return 0;
      }
      sigprocmask(SIG_UNBLOCK,&vtalrm,NULL);
      return -1;
}

void stub(void *(* start_routine)(void *), void *arg) {
  void *ret_val = (*start_routine)(arg);
  int current_tid = uthread_self();
  sigprocmask(SIG_BLOCK,&vtalrm,NULL);
  TCB *current_tcb = global_thread_directory->operator[](current_tid);
  current_tcb->set_return_value(ret_val);
  cout<<endl<<"Thread "<<*(int *)ret_val<<" exited."<<endl;
  if(uthread_terminate(current_tid)==-1) {
    cout<<endl<<"Thread "<<current_tid<<" termination failed."<<endl;
  }
  sigprocmask(SIG_UNBLOCK,&vtalrm,NULL);
}

void *thread_function(int *i) {
  static int c = 0;
  while(1);
    // {
  //   c++;
  //   cout<<endl<<"In thread :"<<*i<<endl;
  //   if (c % 3 == 0) {
  //     cout<<endl<<"Switching from thread :"<<*i<<endl;
  //     uthread_yield();
  //   }
  //   usleep(SECOND);
  // }
}

void *thread_function2(int *i) {
  int c = 0;
  while(c<=10);
    // {
  //   c++;
  //   cout<<endl<<"In thread :"<<*i<<endl;
  //   if (c % 3 == 0) {
  //     cout<<endl<<"Switching from thread :"<<*i<<endl;
  //     uthread_yield();
  //   }
  //   usleep(SECOND);
  // }
  return (void *)i;
}

int main() {

  uthread_init(2000);
  main_thread_setup();
  int *arg = new int;
  *arg = 10;
  uthread_create(*(void *(*)(void*))&thread_function, (void *)arg);

  //int *arg2 = new int;
  //*arg2 = 20;
  //uthread_create(*(void *(*)(void*))&thread_function, (void *)arg2);

  int *arg3 = new int;
  *arg3 = 30;
  uthread_create(*(void *(*)(void*))&thread_function, (void *)arg3);

  int *arg4 = new int;
  *arg4 = 40;
  uthread_create(*(void *(*)(void*))&thread_function2, (void *)arg4);

  setitimer (ITIMER_VIRTUAL, &timer, NULL);

  //int *ret_val = NULL;
  //if(uthread_join(4, (void **)&ret_val) == 0) {
  //cout<<endl<<"thread 4 done "<<*ret_val<<endl;
    //}

  //static int c = 0;
  while(1);
  // {
  //   c++;
  //   cout<<endl<<"In main thread:"<<c<<endl;
  //   if (c % 3 == 0) {
  //     cout<<endl<<"Switching from thread :Main thread"<<endl;
  //     uthread_yield();
  //   }
  //   usleep(SECOND);
  // }
  return 0;
}
