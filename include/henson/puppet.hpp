#ifndef HENSON_PUPPET_HPP
#define HENSON_PUPPET_HPP

#include <vector>
#include <stdexcept>
#include <iostream>

#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

#include <mpi.h>
#include <boost/context/all.hpp>

#include <format.h>
#include <henson/data.hpp>
#include <henson/procs.hpp>

namespace henson
{
namespace bc = boost::context;

struct Puppet
{
    typedef             int  (*MainType)(int argc, char *argv[]);
    typedef             void (*SetContextType)(void* parent, void* local);
    typedef             void (*SetWorldType)(MPI_Comm world);
    typedef             void (*SetNameMapType)(void* namemap);
    typedef             void (*SetProcMapType)(void* procmap);
    typedef             void (*SetStopType)(int* stop);

                        Puppet(const std::string& fn, int argc, char** argv, ProcMap* procmap, NameMap* namemap):
                            filename_(fn),
                            argc_(argc), argv_(argv)
                        {
                            void* lib = dlopen(fn.c_str(), RTLD_LAZY);

                            main_ = get_function<MainType>(lib, "main");

                            get_function<SetContextType>(lib, "henson_set_contexts")(&from_, &to_);
                            get_function<SetProcMapType>(lib, "henson_set_procmap")(procmap);
                            get_function<SetStopType>   (lib, "henson_set_stop")(&stop_);

                            try
                            {
                                get_function<SetNameMapType>(lib, "henson_set_namemap")(namemap);
                            } catch(std::runtime_error& e)
                            {
                                // it's a weird situation, but possible if the puppet doesn't need to get any data in or out
                                // (the linker may choose not to pull src/data.cpp and so henson_set_namemap will be missing)
                                fmt::print(std::cerr, "Warning: {}", e.what());
                                fmt::print(std::cerr, "Reasonable only if {} doesn't need to exchange any data\n", filename_);
                            }

                            stack_ = allocator_.allocate();
                            to_ = bc::make_fcontext(stack_.sp, stack_.size, exec);
                        }

                        ~Puppet()               { allocator_.deallocate(stack_); }

    // can't even move a puppet since the addresses of its from_ and to_ fields
    // are stored in the modules (saved via henson_set_context, in the constructor above)
                        Puppet(const Puppet&)   =delete;
                        Puppet(Puppet&&)        =delete;

    Puppet&             operator=(const Puppet&)=delete;
    Puppet&             operator=(Puppet&&)     =delete;

    void                proceed()               { bc::jump_fcontext(&from_, to_, (intptr_t) this); }
    void                yield()                 { bc::jump_fcontext(&to_, from_, 0); }

    void                signal_stop()           { stop_ = 1; }

    bool                running() const         { return running_; }
    int                 result() const          { return result_; }

    static void         exec(intptr_t self_)    { while(true) { Puppet* self = (Puppet*) self_; self->running_ = true; self->result_ = self->main_(self->argc_,self->argv_); self->running_ = false; self->yield(); } }

    template<class T>
    T                   get_function(void* lib, const char* name)
    {
        T f = (T) dlsym(lib, name);
        if (f == NULL)
            throw std::runtime_error(fmt::format("Could not load {}() in {}\n{}\n", name, filename_, dlerror()));
        return f;
    }

    std::string         filename_;
    int                 argc_;
    char**              argv_;
    bc::stack_context   stack_;
    bc::fixedsize_stack allocator_;
    //bc::protected_fixedsize_stack allocator_;

    MainType            main_;
    bc::fcontext_t      from_, to_;
    bool                running_;
    int                 stop_ = 0;
    int                 result_ = -1;
};


}

#endif
