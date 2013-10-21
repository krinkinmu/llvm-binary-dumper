#include <llvm/ADT/OwningPtr.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/Triple.h>

#include <llvm/Object/ObjectFile.h>

#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>
#include <llvm/MC/MCObjectFileInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCInst.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCDisassembler.h>
#include <llvm/MC/MCRegisterInfo.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/MemoryObject.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/Debug.h>

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>


namespace {

    using namespace llvm;
    using namespace object;

    class StringRefMemoryObject : public MemoryObject
    {
        StringRef bytes;
    public:
        StringRefMemoryObject(StringRef bytes) : bytes(bytes) {}

        uint64_t getBase() const { return 0; }
        uint64_t getExtent() const { return bytes.size(); }

        int readByte(uint64_t addr, uint8_t *byte) const
        {
            if (addr >= getExtent())
                return -1;
            *byte = bytes[addr];
            return 0;
        }
    };

    bool error(error_code ec)
    {
        if (!ec) return false;

        errs() << "error: " << ec.message() << "\n";
        errs().flush();
        return true;
    }

    const Target * get_target(ObjectFile const *obj, std::string &tname)
    {
        Triple triple("unknown-unknown-unknown");
        triple.setArch(Triple::ArchType(obj->getArch()));

        std::string err;
        Target const *target = TargetRegistry::lookupTarget(triple.getTriple(), err);
        if (!target)
        {
            errs() << obj->getFileName().str() << ": " << err << "\n";
            return 0;
        }

        tname = triple.getTriple();
        return target;
    }

    void disassemble_all(ObjectFile const *obj)
    {
        std::string triple;
        Target const *target = get_target(obj, triple);
        if (!target)
            return;

        error_code ec;
        for (section_iterator it = obj->begin_sections(); it != obj->end_sections(); it.increment(ec))
        {
            if (error(ec)) break;

            bool text;
            if (error(it->isText(text))) break;
            if (!text) continue;

            uint64_t saddr;
            if (error(it->getAddress(saddr))) break;

            std::vector<std::pair<uint64_t, StringRef> > syms;
            for (symbol_iterator sit = obj->begin_symbols(); sit != obj->end_symbols(); sit.increment(ec))
            {
                bool contains;
                if (!error(it->containsSymbol(*sit, contains)) && contains)
                {
                    uint64_t addr;
                    if (error(sit->getAddress(addr))) break;
                    addr -= saddr;

                    StringRef name;
                    if (error(sit->getName(name))) break;
                    syms.push_back(std::make_pair(addr, name));
                }
            }
            array_pod_sort(syms.begin(), syms.end());

            StringRef name;
            if (error(it->getName(name))) break;
            outs() << "disassembly of section " << name << ":\n";

            if (syms.empty())
                syms.push_back(std::make_pair(0, name));

            OwningPtr<const MCAsmInfo> ai(target->createMCAsmInfo(triple));
            if (!ai)
            {
                errs() << "error: no assembly info for target " << triple << "\n";
                return;
            }

            OwningPtr<const MCSubtargetInfo> sti(target->createMCSubtargetInfo(triple, "", ""));
            if (!sti)
            {
                errs() << "error: no subtarget info for target " << triple << "\n";
                return;
            }

            OwningPtr<const MCDisassembler> da(target->createMCDisassembler(*sti));
            if (!da)
            {
                errs() << "error: no disassembler for target " << triple << "\n";
                return;
            }

            OwningPtr<const MCRegisterInfo> mri(target->createMCRegInfo(triple));
            if (!mri)
            {
                errs() << "error: no register info for target " << triple << "\n";
                return;
            }

            OwningPtr<const MCInstrInfo> mii(target->createMCInstrInfo());
            if (!mii)
            {
                errs() << "error: no instruction info for target " << triple << "\n";
                return;
            }

            int apv = ai->getAssemblerDialect();
            OwningPtr<MCInstPrinter> ip(target->createMCInstPrinter(apv, *ai, *mii, *mri, *sti));
            if (!ip)
            {
                errs() << "error: no instruction printer for target " << triple << "\n";
                return;
            }

            StringRef bytes;
            if (error(it->getContents(bytes))) break;
            StringRefMemoryObject memory(bytes);
            uint64_t size, index, ssize;
            if (error(it->getSize(ssize))) break;

            for (size_t si = 0; si != syms.size(); ++si)
            {
                uint64_t start = syms[si].first;
                uint64_t end;
                if (si == syms.size() - 1)
                    end = ssize;
                else if (syms[si + 1].first != start)
                    end = syms[si + 1].first - 1;
                else
                    continue;

                outs() << syms[si].second.str() << ":\n";

                for (index = start; index < end; index += size)
                {
                    MCInst inst;
                    if (da->getInstruction(inst, size, memory, index, nulls(), nulls()))
                    {
                        outs() << format("%8" PRIx64 ":\t", saddr + index);
                        ip->printInst(&inst, outs(), "");
                        outs() << "\n";
                    }
                    else
                    {
                        errs() << "warning: invalid instruction encoding\n";
                        if (size == 0)
                            size = 1;
                    }
                }
            }

            outs() << "\n";
        }
    }

    void dump_impl(ObjectFile const *obj)
    {
        outs() << obj->getFileName()
                  << ":\tfile format " << obj->getFileFormatName()
                  << "\n\n";
        disassemble_all(obj);
    }

    void dump_object(std::string const &file)
    {
        if (!sys::fs::exists(file))
        {
            errs() << "File " << file << ": no such file\n";
            return;
        }

        OwningPtr<Binary> binary;
        if (error_code ec = createBinary(file, binary))
        {
            errs() << "File " << file << ": " << ec.message() << "\n";
            return;
        }

        if (ObjectFile *obj = dyn_cast<ObjectFile>(binary.get()))
            dump_impl(obj);
        else
            errs() << "File " << file << ": unsupported file format\n";
    }

}

int main(int argc, char **argv)
{
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllDisassemblers();

    std::vector<std::string> input_files;
    for (size_t it = 1; it != static_cast<size_t>(argc); ++it)
        input_files.push_back(std::string(argv[it]));

    std::for_each(input_files.begin(), input_files.end(), dump_object);

    return 0;
}
