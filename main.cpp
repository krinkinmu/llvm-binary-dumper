#include <llvm/ADT/OwningPtr.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringExtras.h>

#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <iostream>
#include <vector>
#include <string>


namespace {

    using namespace llvm;
    using namespace object;

    void dump_impl(ObjectFile const *obj)
    {
        std::cout << obj->getFileName().str()
                  << ":\tfile format " << obj->getFileFormatName().str()
                  << std::endl << std::endl;
    }

    void dump_object(std::string const &file)
    {
        if (!sys::fs::exists(file))
        {
            std::cerr << "File " << file << ": no such file" << std::endl;
            return;
        }

        OwningPtr<Binary> binary;
        if (error_code ec = createBinary(file, binary))
        {
            std::cerr << "File " << file << ": " << ec.message() << std::endl;
            return;
        }

        if (ObjectFile *obj = dyn_cast<ObjectFile>(binary.get()))
            dump_impl(obj);
        else
            std::cerr << "File " << file
                      << ": unsupported file format" << std::endl;
    }

}

int main(int argc, char **argv)
{
    std::vector<std::string> input_files;
    for (size_t it = 1; it != static_cast<size_t>(argc); ++it)
        input_files.push_back(std::string(argv[it]));

    std::for_each(input_files.begin(), input_files.end(), dump_object);

    return 0;
}
