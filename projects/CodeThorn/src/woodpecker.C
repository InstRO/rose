// Author: Markus Schordan, 2013.

#include "rose.h"

#include "inliner.h"

#include <iostream>
#include "VariableIdMapping.h"
#include "Labeler.h"
#include "AstAnnotator.h"
#include "Miscellaneous.h"
#include "ProgramStats.h"
#include "CommandLineOptions.h"
#include "AnalysisAbstractionLayer.h"
#include "AbstractValue.h"
#include "SgNodeHelper.h"
#include "FIConstAnalysis.h"
#include "TrivialInlining.h"
#include "Threadification.h"
#include "RewriteSystem.h"
#include "Lowering.h"

#include <vector>
#include <set>
#include <list>
#include <string>

#include "SprayException.h"
#include "CodeThornException.h"

#include "limits.h"
#include <cmath>
#include "assert.h"

using namespace std;
using namespace CodeThorn;

#include "Diagnostics.h"
using namespace Sawyer::Message;

#include "PropertyValueTable.h"
#include "DeadCodeElimination.h"
#include "ReachabilityAnalysis.h"

#include "ConversionFunctionsGenerator.h"

//static  VariableIdSet variablesOfInterest;
static bool detailedOutput=0;
const char* csvAssertFileName=0;
const char* csvConstResultFileName=0;
bool global_option_multiconstanalysis=false;

#if 0
bool isVariableOfInterest(VariableId varId) {
  return variablesOfInterest.find(varId)!=variablesOfInterest.end();
}
#endif

size_t numberOfFunctions(SgNode* node) {
  RoseAst ast(node);
  size_t num=0;
  for(RoseAst::iterator i=ast.begin();i!=ast.end();i++) {
    if(isSgFunctionDefinition(*i))
      num++;
  }
  return num;
}

void printResult(VariableIdMapping& variableIdMapping, VarConstSetMap& map) {
  cout<<"Result:"<<endl;
  VariableConstInfo vci(&variableIdMapping, &map);
  for(VarConstSetMap::iterator i=map.begin();i!=map.end();++i) {
    VariableId varId=(*i).first;
    //string variableName=variableIdMapping.uniqueVariableName(varId);
    string variableName=variableIdMapping.variableName(varId);
    set<AbstractValue> valueSet=(*i).second;
    stringstream setstr;
    setstr<<"{";
    for(set<AbstractValue>::iterator i=valueSet.begin();i!=valueSet.end();++i) {
      if(i!=valueSet.begin())
        setstr<<",";
      setstr<<(*i).toString();
    }
    setstr<<"}";
    cout<<variableName<<"="<<setstr.str()<<";";
#if 1
    cout<<"Range:"<<VariableConstInfo::createVariableValueRangeInfo(varId,map).toString();
    cout<<" width: "<<VariableConstInfo::createVariableValueRangeInfo(varId,map).width().toString();
    cout<<" top: "<<VariableConstInfo::createVariableValueRangeInfo(varId,map).isTop();
    cout<<endl;
#endif
    cout<<" isAny:"<<vci.isAny(varId)
        <<" isUniqueConst:"<<vci.isUniqueConst(varId)
        <<" isMultiConst:"<<vci.isMultiConst(varId);
    if(vci.isUniqueConst(varId)||vci.isMultiConst(varId)) {
      cout<<" width:"<<vci.width(varId);
    } else {
      cout<<" width:unknown";
    }
    cout<<" Test34:"<<vci.isInConstSet(varId,34);
    cout<<endl;
  }
  cout<<"---------------------"<<endl;
}


void printCodeStatistics(SgNode* root) {
  SgProject* project=isSgProject(root);
  VariableIdMapping variableIdMapping;
  variableIdMapping.computeVariableSymbolMapping(project);
  VariableIdSet setOfUsedVars=AnalysisAbstractionLayer::usedVariablesInsideFunctions(project,&variableIdMapping);
  DeadCodeElimination dce;
  cout<<"----------------------------------------------------------------------"<<endl;
  cout<<"Statistics:"<<endl;
  cout<<"Number of empty if-statements: "<<dce.listOfEmptyIfStmts(root).size()<<endl;
  cout<<"Number of functions          : "<<SgNodeHelper::listOfFunctionDefinitions(project).size()<<endl;
  cout<<"Number of global variables   : "<<SgNodeHelper::listOfGlobalVars(project).size()<<endl;
  cout<<"Number of used variables     : "<<setOfUsedVars.size()<<endl;
  cout<<"----------------------------------------------------------------------"<<endl;
  cout<<"VariableIdMapping-size       : "<<variableIdMapping.getVariableIdSet().size()<<endl;
  cout<<"----------------------------------------------------------------------"<<endl;
}

int main(int argc, char* argv[]) {
  ROSE_INITIALIZE;

  Rose::Diagnostics::mprefix->showProgramName(false);
  Rose::Diagnostics::mprefix->showThreadId(false);
  Rose::Diagnostics::mprefix->showElapsedTime(false);

  Sawyer::Message::Facility logger;
  Rose::Diagnostics::initAndRegister(&logger, "Woodpecker");

  try {
    if(argc==1) {
      logger[ERROR] << "wrong command line options."<<endl;
      exit(1);
    }
#if 0
    if(argc==3) {
      csvAssertFileName=argv[2];
      argc=2; // don't confuse ROSE command line
      cout<< "INIT: CSV-output file: "<<csvAssertFileName<<endl;
    }
#endif

  // Command line option handling.
#ifdef USE_SAWYER_COMMANDLINE
    namespace po = Sawyer::CommandLine::Boost;
#else
    namespace po = boost::program_options;
#endif

    po::options_description desc
    ("Woodpecker V0.1\n"
     "Written by Markus Schordan\n"
     "Supported options");

  desc.add_options()
    ("help,h", "produce this help message.")
    ("rose-help", "show help for compiler frontend options.")
    ("version,v", "display the version.")
    ("stats", "display code statistics.")
    ("normalize", po::value< bool >()->default_value(false)->implicit_value(true), "normalize code (eliminate compound assignment operators).")
    ("lowering", po::value< bool >()->default_value(false)->implicit_value(true), "apply lowering to code (eliminates for,while,do-while,continue,break; inlines functions).")
    ("inline", po::value< bool >()->default_value(true)->implicit_value(true), "perform inlining.")
    ("eliminate-empty-if", po::value< bool >()->default_value(true)->implicit_value(true), "eliminate if-statements with empty branches in main function.")
    ("eliminate-dead-code", po::value< bool >()->default_value(true)->implicit_value(true), "eliminate dead code (variables and expressions).")
    ("csv-const-result",po::value< string >(), "generate csv-file <arg> with const-analysis data.")
    ("generate-transformed-code",po::value< bool >()->default_value(true)->implicit_value(true), "generate transformed code with prefix \"rose_\".")
    ("verbose", po::value< bool >()->default_value(false)->implicit_value(true), "print detailed output during analysis and transformation.")
    ("generate-conversion-functions","generate code for conversion functions between variable names and variable addresses.")
    ("csv-assert",po::value< string >(), "name of csv file with reachability assert results'")
    ("enable-multi-const-analysis", po::value< bool >()->default_value(false)->implicit_value(true), "enable multi-const analysis.")
    ("transform-thread-variable", "transform code to use additional thread variable.")
    ("log-level",po::value< string >()->default_value("none,>=warn"),"Set the log level (\"x,>=y\" with x,y in: (none|info|warn|trace|debug)).")
    ;
  //    ("int-option",po::value< int >(),"option info")


  po::store(po::command_line_parser(argc, argv).
            options(desc).allow_unregistered().run(), args);
  po::notify(args);

  if (args.count("help")) {
    cout << "woodpecker <filename> [OPTIONS]"<<endl;
    cout << desc << "\n";
    exit(0);
  }
  if (args.count("rose-help")) {
    argv[1] = strdup("--help");
  }

  if (args.count("version")) {
    cout << "Woodpecker version 0.1\n";
    cout << "Written by Markus Schordan 2013\n";
    exit(0);
  }
  if (args.count("csv-assert")) {
    csvAssertFileName=args["csv-assert"].as<string>().c_str();
  }
  if (args.count("csv-const-result")) {
    csvConstResultFileName=args["csv-const-result"].as<string>().c_str();
  }

  if(args.getBool("verbose"))
    detailedOutput=1;

  mfacilities.control(args["log-level"].as<string>());
  logger[TRACE] << "Log level is " << args["log-level"].as<string>() << endl;

  // clean up string-options in argv
  for (int i=1; i<argc; ++i) {
    if (string(argv[i]) == "--csv-assert"
        || string(argv[i]) == "--csv-const-result"
        ) {
      // do not confuse ROSE frontend
      argv[i] = strdup("");
      assert(i+1<argc);
        argv[i+1] = strdup("");
    }
  }

  global_option_multiconstanalysis=args.getBool("enable-multi-const-analysis");
#if 0
  if(global_option_multiconstanalysis) {
    cout<<"INFO: Using flow-insensitive multi-const-analysis."<<endl;
  } else {
    cout<<"INFO: Using flow-insensitive unique-const-analysis."<<endl;
  }
#endif

  logger[TRACE] << "INIT: Parsing and creating AST started."<<endl;
  SgProject* root = frontend(argc,argv);
  //  AstTests::runAllTests(root);
  // inline all functions
  logger[TRACE] << "INIT: Parsing and creating AST finished."<<endl;

  if(args.count("stats")) {
    printCodeStatistics(root);
    exit(0);
  }

  if(args.getBool("lowering")) {
    logger[TRACE] <<"STATUS: Lowering started."<<endl;
    SPRAY::Lowering lowering;
    lowering.lowerAst(root);
    logger[TRACE] <<"STATUS: Lowering finished."<<endl;
    root->unparse(0,0);
    exit(0);
  }

  VariableIdMapping variableIdMapping;
  variableIdMapping.computeVariableSymbolMapping(root);

  if(args.count("transform-thread-variable")) {
    Threadification* threadTransformation=new Threadification(&variableIdMapping);
    threadTransformation->transform(root);
    root->unparse(0,0);
    delete threadTransformation;
    logger[TRACE] <<"STATUS: generated program with introduced thread-variable."<<endl;
    exit(0);
  }

  SgFunctionDefinition* mainFunctionRoot=0;
  if(args.getBool("inline")) {
    logger[TRACE] <<"STATUS: eliminating non-called trivial functions."<<endl;
    // inline functions
    TrivialInlining tin;
    tin.setDetailedOutput(detailedOutput);
    tin.inlineFunctions(root);
    DeadCodeElimination dce;
    // eliminate non called functions
    int numEliminatedFunctions=dce.eliminateNonCalledTrivialFunctions(root);
    logger[TRACE] <<"STATUS: eliminated "<<numEliminatedFunctions<<" functions."<<endl;
  } else {
    logger[INFO] <<"Inlining: turned off."<<endl;
  }

  if(args.getBool("eliminate-empty-if")) {
    DeadCodeElimination dce;
    logger[TRACE] <<"STATUS: Eliminating empty if-statements."<<endl;
    size_t num=0;
    size_t numTotal=num;
    do {
      num=dce.eliminateEmptyIfStmts(root);
      logger[INFO] <<"Number of if-statements eliminated: "<<num<<endl;
      numTotal+=num;
    } while(num>0);
    logger[TRACE] <<"STATUS: Total number of empty if-statements eliminated: "<<numTotal<<endl;
  }

  if(args.getBool("normalize")) {
    logger[TRACE] <<"STATUS: Lowering started."<<endl;
    RewriteSystem rewriteSystem;
    rewriteSystem.resetStatistics();
    rewriteSystem.rewriteCompoundAssignmentsInAst(root,&variableIdMapping);
    logger[TRACE] <<"STATUS: Lowering finished."<<endl;
  }

  logger[TRACE] <<"STATUS: performing flow-insensitive const analysis."<<endl;
  VarConstSetMap varConstSetMap;
  VariableIdSet variablesOfInterest;
  FIConstAnalysis fiConstAnalysis(&variableIdMapping);
  fiConstAnalysis.setOptionMultiConstAnalysis(global_option_multiconstanalysis);
  fiConstAnalysis.setDetailedOutput(detailedOutput);
  fiConstAnalysis.runAnalysis(root, mainFunctionRoot);
  variablesOfInterest=fiConstAnalysis.determinedConstantVariables();
  logger[INFO] <<"variables of interest: "<<variablesOfInterest.size()<<endl;
  if(detailedOutput)
    printResult(variableIdMapping,varConstSetMap);

  if(csvConstResultFileName) {
    VariableIdSet setOfUsedVarsInFunctions=AnalysisAbstractionLayer::usedVariablesInsideFunctions(root,&variableIdMapping);
    VariableIdSet setOfUsedVarsGlobalInit=AnalysisAbstractionLayer::usedVariablesInGlobalVariableInitializers(root,&variableIdMapping);
    VariableIdSet setOfAllUsedVars = setOfUsedVarsInFunctions;
    setOfAllUsedVars.insert(setOfUsedVarsGlobalInit.begin(), setOfUsedVarsGlobalInit.end());
    logger[INFO]<<"number of used vars inside functions: "<<setOfUsedVarsInFunctions.size()<<endl;
    logger[INFO]<<"number of used vars in global initializations: "<<setOfUsedVarsGlobalInit.size()<<endl;
    logger[INFO]<<"number of vars inside functions or in global inititializations: "<<setOfAllUsedVars.size()<<endl;
    fiConstAnalysis.filterVariables(setOfAllUsedVars);
    fiConstAnalysis.writeCvsConstResult(variableIdMapping, string(csvConstResultFileName));
  }

  if(args.count("generate-conversion-functions")) {
    string conversionFunctionsFileName="conversionFunctions.C";
    ConversionFunctionsGenerator gen;
    set<string> varNameSet;
    std::list<SgVariableDeclaration*> globalVarDeclList=SgNodeHelper::listOfGlobalVars(root);
    for(std::list<SgVariableDeclaration*>::iterator i=globalVarDeclList.begin();i!=globalVarDeclList.end();++i) {
      SgInitializedNamePtrList& initNamePtrList=(*i)->get_variables();
      for(SgInitializedNamePtrList::iterator j=initNamePtrList.begin();j!=initNamePtrList.end();++j) {
	      SgInitializedName* initName=*j;
        if ( true || isSgArrayType(initName->get_type()) ) {  // optional filter (array variables only)
	        SgName varName=initName->get_name();
	        string varNameString=varName; // implicit conversion
	        varNameSet.insert(varNameString);
        }
      }
    }
    string code=gen.generateCodeForGlobalVarAdressMaps(varNameSet);
    ofstream myfile;
    myfile.open(conversionFunctionsFileName.c_str());
    myfile<<code;
    myfile.close();
  }

  VariableConstInfo vci=*(fiConstAnalysis.getVariableConstInfo());
  DeadCodeElimination dce;
  if(args.getBool("eliminate-dead-code")) {
    logger[TRACE]<<"STATUS: performing dead code elimination."<<endl;
    dce.setDetailedOutput(detailedOutput);
    dce.setVariablesOfInterest(variablesOfInterest);
    dce.eliminateDeadCodePhase1(root,&variableIdMapping,vci);
    logger[TRACE]<<"STATUS: Eliminated "<<dce.numElimVars()<<" variable declarations."<<endl;
    logger[TRACE]<<"STATUS: Eliminated "<<dce.numElimAssignments()<<" variable assignments."<<endl;
    logger[TRACE]<<"STATUS: Replaced "<<dce.numElimVarUses()<<" uses of variables with constant."<<endl;
    logger[TRACE]<<"STATUS: Eliminated "<<dce.numElimVars()<<" dead variables."<<endl;
    logger[TRACE]<<"STATUS: Dead code elimination phase 1: finished."<<endl;
    logger[TRACE]<<"STATUS: Performing condition const analysis."<<endl;
  } else {
    logger[TRACE]<<"STATUS: Dead code elimination: turned off."<<endl;
  }
  if(csvAssertFileName) {
    logger[TRACE]<<"STATUS: performing flow-insensensitive condition-const analysis."<<endl;
    Labeler labeler(root);
    fiConstAnalysis.performConditionConstAnalysis(&labeler);
    logger[INFO]<<"Number of true-conditions     : "<<fiConstAnalysis.getTrueConditions().size()<<endl;
    logger[INFO]<<"Number of false-conditions    : "<<fiConstAnalysis.getFalseConditions().size()<<endl;
    logger[INFO]<<"Number of non-const-conditions: "<<fiConstAnalysis.getNonConstConditions().size()<<endl;
    logger[TRACE]<<"STATUS: performing flow-insensensitive reachability analysis."<<endl;
    ReachabilityAnalysis ra;
    PropertyValueTable reachabilityResults=ra.fiReachabilityAnalysis(labeler, fiConstAnalysis);
    logger[TRACE]<<"STATUS: generating file "<<csvAssertFileName<<endl;
    reachabilityResults.writeFile(csvAssertFileName,true);
  }
#if 0
  rdAnalyzer->determineExtremalLabels(startFunRoot);
  rdAnalyzer->run();
#endif
  logger[INFO]<< "Remaining functions in program: "<<numberOfFunctions(root)<<endl;
  if(args.getBool("generate-transformed-code")) {
    logger[TRACE]<< "STATUS: generating transformed source code."<<endl;
    root->unparse(0,0);
  }

  logger[TRACE]<< "STATUS: finished."<<endl;

  // main function try-catch
  } catch(CodeThorn::Exception& e) {
    cerr << "CodeThorn::Exception raised: " << e.what() << endl;
    mfacilities.shutdown();
    return 1;
  } catch(SPRAY::Exception& e) {
    cerr << "Spray::Exception raised: " << e.what() << endl;
    mfacilities.shutdown();
    return 1;
  } catch(std::exception& e) {
    cerr << "std::exception raised: " << e.what() << endl;
    mfacilities.shutdown();
    return 1;
  } catch(char* str) {
    cerr << "*Exception raised: " << str << endl;
    mfacilities.shutdown();
    return 1;
  } catch(const char* str) {
    cerr << "Exception raised: " << str << endl;
    mfacilities.shutdown();
    return 1;
  } catch(string str) {
    cerr << "Exception raised: " << str << endl;
    mfacilities.shutdown();
    return 1;
  }
  mfacilities.shutdown();
  return 0;
}
