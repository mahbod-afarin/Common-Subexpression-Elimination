#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/User.h"
#include "llvm/IR/IRBuilder.h"
#include <string>
#include <fstream>
#include <unordered_map>
#include <set>
#include <queue>
#include <string>
#include<vector>

#define set_iss set<std::pair<int,std::pair<std::string,std::string>>>

using namespace llvm;
using namespace std;

#define DEBUG_TYPE "CSElimination"

struct CSElimination : public FunctionPass
{
    static char ID;
    CSElimination() : FunctionPass(ID) {}

    bool exists_remove(set_iss &s, std::string x)
    {
        set_iss::iterator it;
        for (it = s.begin(); it != s.end(); it++)
        {
            if((it->second).first == x || (it->second).second == x)
            {
                s.erase(it);
                return true;
            }
        }
        return false;
    }

    string ref_To_string(StringRef &s){
        string res = "";
        for (int i = 0; i < s.size(); i++)
            res += s[i];
        return res;
    }

    void addAll(set_iss &s1, set_iss &s2)
    {
        set_iss::iterator it;
        for (it = s1.begin(); it != s1.end(); it++)
        {
            s2.insert(*it);
        }
    }

    bool exists(set < std::string > &s, std::string &a, std::string &b){
        if(s.find(a)!=s.end() || s.find(b)!=s.end())
            return true;
        return false;
    }

    void change_parent(BasicBlock &basic_block, std::pair<int, std::pair<std::string, std::string> > operation, map<BasicBlock *,  set_iss> &IN, map<BasicBlock *,  set_iss> &OUT, map<std::pair<int, std::pair<std::string, std::string> >, Value*> op_name_map){
        BasicBlock *BB = &basic_block;
        for (auto it = pred_begin(BB), et = pred_end(BB); it != et; ++it)
        {
            BasicBlock* predecessor = *it;
            set_iss in = IN.find(predecessor)->second;
            set_iss out = OUT.find(predecessor)->second;
            if(in.find(operation) == in.end() && out.find(operation) != out.end())
            {
                // errs() << "change inside :" << (*predecessor).getName() << "\n";
                Instruction* temp;
                std::string firstLoad, secondLoad;
                for (Instruction &I : *predecessor)
                {
                    if(I.getOpcode() == Instruction::Load)
                    {
                        secondLoad = firstLoad;
                        StringRef varName = I.getOperand(0)->getName();
                        firstLoad = ref_To_string(varName);
                    }
                    if (I.getOpcode() == Instruction::Add || I.getOpcode() == Instruction::Mul || I.getOpcode() == Instruction::Sub || I.getOpcode() == Instruction::SDiv)
                    {
                        string a[2];
                        if(isa<Constant>(I.getOperand(0)))
                        {
                            llvm::ConstantInt* CI = dyn_cast<ConstantInt>(I.getOperand(0));
                            a[0] = (to_string(CI->getSExtValue()));
                            if(!isa<Constant>(I.getOperand(1)))
                            {
                                a[1] = firstLoad;
                            }
                        }
                        else
                        {
                            a[0] = firstLoad;
                            if(isa<Constant>(I.getOperand(1)))
                            {
                                llvm::ConstantInt* CI = dyn_cast<ConstantInt>(I.getOperand(1));
                                a[1] = to_string(CI->getSExtValue());
                            }
                            else
                            {
                                a[1] = secondLoad;
                            }
                        }
                        if(I.getOpcode() == operation.first && a[0] == operation.second.first && a[1] == operation.second.second)
                            temp = &I;
                    }
                }

                // errs() << "temp is equal to " << *temp << "\n";

                // temp
                Value* result = create_variable(op_name_map, operation, *temp);
                auto x = temp->getNextNode();
                IRBuilder<> Builder(x);
                Value* result2 = Builder.CreateStore(x->getOperand(0), result);
                Value* result3 = Builder.CreateLoad(temp->getOperand(0)->getType(), result);
                Value* result4 = Builder.CreateStore(result3, x->getOperand(1));
                // I.eraseFromParent();
                x->eraseFromParent();
            }
            else
            {
                change_parent(*predecessor, operation, IN, OUT, op_name_map);
            }
        }
    }

    Value* create_variable(map<std::pair<int, std::pair<std::string, std::string> >, Value*> &op_name_map, std::pair<int, std::pair<std::string, std::string> > &operation, Instruction &I){
        // errs() << "operation " << operation.first << "\n";
        if(op_name_map.find(operation) != op_name_map.end())
        {
            // errs() << "HERE!!!" << "\n";
            return (op_name_map.find(operation)->second);
        }
        else
        {
            BasicBlock *bb = I.getParent();
            while(pred_begin(bb) != pred_end(bb))
                bb = *pred_begin(bb);
            for (Instruction &temp: *bb)
            {
                IRBuilder<> Builder(&temp);
                std::string s = "temp" + std::to_string(op_name_map.size());
                Value* result = Builder.CreateAlloca(I.getOperand(0)->getType(), nullptr, s);
                op_name_map.insert(std::make_pair(operation, result));
                return result;
                break;
            }
        }
        return nullptr;
    }

    bool runOnFunction(Function &F) override
    {
        errs() << "CSE\n";
        vector<int> vect;
        vector<std::string> vect2;
        vector<string> vect3;

        map<BasicBlock *, set < std::pair<int, std::pair<std::string, std::string> > > > generated_expressions;
        map<BasicBlock *, set<std::string>> killed_variables;
        set < std::pair<int, std::pair<std::string, std::string> > > all_exp;// TOP element of our logic

        for (BasicBlock &basic_block : F)
        {
            set < std::pair<int, std::pair<std::string, std::string> > > temp_generated_expressions;
            set < std::string > temp_killed_variables;
            // errs() << basic_block.getName() << "\n";
            bool flag = false;
            std::string firstLoad, secondLoad;
            for (Instruction &I : basic_block)
            {
                // errs() << I << "\n";
                // if operation add to gen
                if(I.getOpcode() == Instruction::Load)
                {
                    // errs() << "HERE \n";
                    secondLoad = firstLoad;
                    // errs() << I.getOperand(0)->getName() << "HERE2 \n";
                    StringRef varName = I.getOperand(0)->getName();
                    firstLoad = ref_To_string(varName);
                    // errs() << "NOT HERE \n";
                }
                if (I.getOpcode() == Instruction::Add || I.getOpcode() == Instruction::Mul || I.getOpcode() == Instruction::Sub || I.getOpcode() == Instruction::SDiv)
                {
                    string a[2];
                    if(isa<Constant>(I.getOperand(0)))
                    {
                        llvm::ConstantInt* CI = dyn_cast<ConstantInt>(I.getOperand(0));
                        a[0] = (to_string(CI->getSExtValue()));
                        if(!isa<Constant>(I.getOperand(1)))
                        {
                            a[1] = firstLoad;
                            temp_generated_expressions.insert(std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1])));
                            all_exp.insert(std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1])));
                        }
                    }
                    else
                    {
                        a[0] = firstLoad;
                        if(isa<Constant>(I.getOperand(1)))
                        {
                            llvm::ConstantInt* CI = dyn_cast<ConstantInt>(I.getOperand(1));
                            // int s = CI->getSExtValue();
                            a[1] = to_string(CI->getSExtValue());
                            // StringRef* s = new StringRef(temp);
                            // vect.push_back(CI->getSExtValue());
                            // vect2.push_back(ref_To_string(*s));
                            // vect3.push_back(*s);
                            // char w = (*s)[0];
                            // errs() << (*s).size() << "hi" << '\n';
                            // temp_generated_expressions.insert(std::make_pair(I.getOpcode(), std::make_pair(firstLoad,  *s)));
                            // all_exp.insert(std::make_pair(I.getOpcode(), std::make_pair(firstLoad, *s)));
                        }
                        else
                        {
                            a[1] = secondLoad;
                            // temp_generated_expressions.insert(std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1])));
                            // all_exp.insert(std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1])));
                        }
                        temp_generated_expressions.insert(std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1])));
                        all_exp.insert(std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1])));
                    }
                }

                if(I.getOpcode() == Instruction::Store)//kill
                {
                    StringRef varName = I.getOperand(1)->getName();
                    string s = ref_To_string(varName);
                    while(true)//remove from this block generators
                    {
                        if(!exists_remove(temp_generated_expressions, s))
                            break;
                    }

                    //add to the temp_killed_variables
                    temp_killed_variables.insert(s);
                }
            }

            //update the basic block kills and gens with temp_killed and temp_gen
            generated_expressions.insert(std::pair<BasicBlock *, set_iss>(&basic_block, temp_generated_expressions));
            killed_variables.insert(std::pair<BasicBlock *, set<std::string>>(&basic_block, temp_killed_variables));

            // errs() << basic_block.getName() << ":\n";
            // errs() << "generates: \n";
            // set_iss ins = generated_expressions.find(&basic_block)->second;
            // set_iss::iterator it;
            // for (it = ins.begin(); it != ins.end(); it++)
            // {
            //     errs() << it->first << ' ' << it->second.first << ' ' << it->second.second << "\n";
            // }
            // errs() << "\n kills: \n";
            // set<std::string> outs = killed_variables.find(&basic_block)->second;
            // for (set<std::string>::iterator it2 = outs.begin(); it2 != outs.end(); it2++)
            // {
            //     errs() << *it2 << "\n";
            // }
        }
        // errs()<< generated_expressions.size() << "\n\n\n";
        // for (int i = 0; i < vect.size(); i++)
        //     errs() << vect[i] << ' ' << vect2[i] << ' ' << vect3[i] << "\n";
        // errs() << "\n\n\n";

        // errs() << "ALL \n";
        // set_iss::iterator it;
        // for (it = all_exp.begin(); it != all_exp.end(); it++)
        // {
        //     errs() << it->first << ' ' << it->second.first << ' ' << it->second.second << "\n";
        // }

        //Available expressions
        map<BasicBlock *, set < std::pair<int, std::pair<std::string, std::string> > > > IN; // in slides it is refered to as IN[]
        map<BasicBlock *, set < std::pair<int, std::pair<std::string, std::string> > > > OUT; // in slides it is refered to as OUT[]
        bool isFirstHandled = false;
        for (BasicBlock &basic_block : F)
        {
            if(!isFirstHandled)
            {
                set_iss s;
                IN.insert(std::pair<BasicBlock*, set_iss>(&basic_block, s));
                set_iss Bs_gen_exp = generated_expressions.find(&basic_block)->second;
                set_iss temp;
                addAll(Bs_gen_exp, temp);
                OUT.insert(std::pair<BasicBlock*, set_iss>(&basic_block, temp));
                isFirstHandled = true;
            }
            else{
                set_iss temp;
                addAll(all_exp, temp);
                set_iss s;
                IN.insert(std::pair<BasicBlock*, set_iss>(&basic_block, s));
                OUT.insert(std::pair<BasicBlock*, set_iss>(&basic_block, temp));
            }
        }


        

        //find live expressions
        bool change = true;
        while(change)
        {
            // errs() << "here \n";
            change = false;
            isFirstHandled = false;
            for (BasicBlock &basic_block : F)
            {
                // errs() << basic_block.getName() << " \n";
                if(!isFirstHandled)
                {
                    isFirstHandled = true;
                    continue;
                }
                else{
                    set_iss intersect, temp_intersect;
                    addAll(all_exp, intersect);
                    addAll(all_exp, temp_intersect);
                    BasicBlock *BB = &basic_block;
                    // errs() << "intersect.size " << intersect.size() << "\n";
                    for (auto it = pred_begin(BB), et = pred_end(BB); it != et; ++it)
                    {
                        temp_intersect.clear();
                        BasicBlock* predecessor = *it;
                        set_iss pred_out = OUT.find(predecessor)->second;
                        std::set_intersection(intersect.begin(), intersect.end(), pred_out.begin(), pred_out.end(), std::inserter(temp_intersect,temp_intersect.begin()));
                        intersect.clear();
                        addAll(temp_intersect, intersect);
                    }
                    set_iss temp;
                    addAll(intersect, temp);
                    IN.erase(IN.find(&basic_block));
                    IN.insert(std::pair<BasicBlock*, set_iss>(&basic_block, temp));
                    set<std::string>::iterator it;
                    set<std::string> temp_kill = killed_variables.find(&basic_block)->second;
                    for(it = temp_kill.begin(); it!=temp_kill.end(); it++)
                    {
                        exists_remove(intersect, *it);
                    }
                    
                    set_iss temp_gen = generated_expressions.find(&basic_block)->second;
                    set_iss::iterator it2;
                    for (it2 = temp_gen.begin(); it2 != temp_gen.end(); it2++)
                    {
                        intersect.insert(*it2);
                    }

                    if(OUT.find(&basic_block)->second != intersect)
                    {
                        change = true;
                    }

                    OUT.find(&basic_block)->second = intersect;
                }
            }
        }

        // for (BasicBlock &basic_block : F)
        // {
        //     errs() << basic_block.getName() << ":\n";
        //     errs() << "INS: \n";
        //     set_iss ins = IN.find(&basic_block)->second;
        //     set_iss::iterator it;
        //     for (it = ins.begin(); it != ins.end(); it++)
        //     {
        //         errs() << it->first << ' ' << it->second.first << ' ' << it->second.second << "\n";
        //     }
        //     errs() << "\nouts: \n\n";
        //     set_iss outs = OUT.find(&basic_block)->second;
        //     for (it = outs.begin(); it != outs.end(); it++)
        //     {
        //         errs() << it->first << ' ' << it->second.first << ' ' << it->second.second << "\n";
        //     }
        // }




        map<std::pair<int, std::pair<std::string, std::string> >, Value*> op_name_map;
        // errs() << "HELLO \n";
        //replace redundants
        change = true;
        while(change){
            change = false;
            for (BasicBlock &basic_block : F)
            {
                if(change)
                    break;

                set < std::string > temp_killed_variables;
                bool flag = false;
                std::string firstLoad, secondLoad;
                for (Instruction &I : basic_block)
                {
                    if(change)
                        break;
                    if(I.getOpcode() == Instruction::Load)
                    {
                        secondLoad = firstLoad;
                        StringRef varName = I.getOperand(0)->getName();
                        firstLoad = ref_To_string(varName);
                    }
                    if (I.getOpcode() == Instruction::Add || I.getOpcode() == Instruction::Mul || I.getOpcode() == Instruction::Sub || I.getOpcode() == Instruction::SDiv)
                    {
                        string a[2];
                        if(isa<Constant>(I.getOperand(0)))
                        {
                            llvm::ConstantInt* CI = dyn_cast<ConstantInt>(I.getOperand(0));
                            a[0] = (to_string(CI->getSExtValue()));
                            if(!isa<Constant>(I.getOperand(1)))
                            {
                                a[1] = firstLoad;
                            }
                        }
                        else
                        {
                            a[0] = firstLoad;
                            if(isa<Constant>(I.getOperand(1)))
                            {
                                llvm::ConstantInt* CI = dyn_cast<ConstantInt>(I.getOperand(1));
                                a[1] = to_string(CI->getSExtValue());
                            }
                            else
                            {
                                a[1] = secondLoad;
                            }
                        }

                        set_iss temp = IN.find(&basic_block)->second;
                        std::pair<int, std::pair<std::string, std::string> > operation = std::make_pair(I.getOpcode(), std::make_pair(a[0], a[1]));
                        // errs()  << "!!!! " << I << " " << exists(temp_killed_variables, a[0], a[1]) << " " << (temp.find(operation)!=temp.end()) << "\n";
                        if(!exists(temp_killed_variables, a[0], a[1]) && temp.find(operation)!=temp.end())
                        {
                            // errs() << "don't need to compute again: " << I << "   " << a[0] << ' ' << I.getName() << ' ' << a[1] << "\n";
                            auto x = I.getNextNode();
                            Value* result = create_variable(op_name_map, operation, I);
                            // Value* result = Builder.CreateAlloca(I.getOperand(0)->getType(), nullptr, "temp");
                            IRBuilder<> Builder(&I);
                            Value* result2 = Builder.CreateLoad(I.getOperand(0)->getType(), result);
                            Value* result3 = Builder.CreateStore(result2, x->getOperand(1));
                            I.eraseFromParent();
                            x->eraseFromParent();
                            change_parent(basic_block, operation, IN, OUT, op_name_map);
                            change = true;
                            break;
                        }
                    }
                    if(I.getOpcode() == Instruction::Store)//kill
                    {
                        StringRef varName = I.getOperand(1)->getName();
                        string s = ref_To_string(varName);
                        //add to the temp_killed_variables
                        temp_killed_variables.insert(s);
                    }
                }
            }
        }


        for (BasicBlock &basic_block : F)
        {
            errs() << basic_block.getName() << ":\n\n";
            for (Instruction &I : basic_block)
            {
                errs() << I << "\n";
            }
        }


        return true; // Indicate this is a Transform pass
    }
}; // end of struct CSElimination // end of anonymous namespace

char CSElimination::ID = 0;
static RegisterPass<CSElimination> X("CSElimination", "CSElimination Pass",
                                     false /* Only looks at CFG */,
                                     true /* Tranform Pass */);
