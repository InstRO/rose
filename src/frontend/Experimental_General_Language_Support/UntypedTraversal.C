#include "sage3basic.h"
#include "UntypedTraversal.h"

#define DEBUG_UNTYPED_TRAVERSAL 0

using namespace Untyped;
using std::cout;
using std::endl;

UntypedTraversal::UntypedTraversal(SgSourceFile* sourceFile, UntypedConverter* converter)
{
   p_source_file = sourceFile;
   pConverter = converter;
}

InheritedAttribute
UntypedTraversal::evaluateInheritedAttribute(SgNode* node, InheritedAttribute currentScope)
{
   switch (node->variantT())
   {
     case V_SgUntypedFile:
       {
          SgSourceFile* sg_file = p_source_file;
          ROSE_ASSERT(sg_file != NULL);
          currentScope = pConverter->initialize_global_scope(sg_file);
          break;
       }
     case V_SgUntypedGlobalScope:
       {
          SgUntypedGlobalScope* ut_scope = dynamic_cast<SgUntypedGlobalScope*>(node);
          SgGlobal* sg_scope = pConverter->convertSgUntypedGlobalScope(ut_scope, SageBuilder::getGlobalScopeFromScopeStack());
          currentScope = sg_scope;
          break;
      }
    case V_SgUntypedModuleDeclaration:
      {
         SgUntypedModuleDeclaration* ut_module = dynamic_cast<SgUntypedModuleDeclaration*>(node);
         pConverter->convertSgUntypedModuleDeclaration(ut_module,currentScope);
         currentScope = SageBuilder::topScopeStack();
         break;
      }
    case V_SgUntypedProgramHeaderDeclaration:
      {
         SgUntypedProgramHeaderDeclaration* ut_program = dynamic_cast<SgUntypedProgramHeaderDeclaration*>(node);
         pConverter->convertSgUntypedProgramHeaderDeclaration(ut_program,currentScope);

      // TODO - think about using SageBuild scope stack (currently used for programs)
         currentScope = SageBuilder::topScopeStack();
         break;
      }
    case V_SgUntypedSubroutineDeclaration:
      {
         SgUntypedSubroutineDeclaration* ut_function = dynamic_cast<SgUntypedSubroutineDeclaration*>(node);
         SgProcedureHeaderStatement* sg_function = pConverter->convertSgUntypedSubroutineDeclaration(ut_function, currentScope);
         currentScope = sg_function->get_definition()->get_body();
         break;
      }
    case V_SgUntypedFunctionDeclaration:
      {
         SgUntypedFunctionDeclaration* ut_function = dynamic_cast<SgUntypedFunctionDeclaration*>(node);
         SgProcedureHeaderStatement* sg_function = pConverter->convertSgUntypedFunctionDeclaration(ut_function, currentScope);
         currentScope = sg_function->get_definition()->get_body();
         break;
      }
    case V_SgUntypedFunctionDeclarationList:
      {
         SgUntypedFunctionDeclarationList* ut_list = dynamic_cast<SgUntypedFunctionDeclarationList*>(node);

      // The list is not converted (needed to add a contains statement) but the current scope may be modified
         pConverter->convertSgUntypedFunctionDeclarationList(ut_list, currentScope);
         break;
      }
    case V_SgUntypedInterfaceDeclaration:
      {
         cout << "--- TODO: convert SgUntypedInterfaceDeclaration\n";
         break;
      }
    case V_SgUntypedBlockDataDeclaration:
      {
         SgUntypedBlockDataDeclaration* ut_block_data = dynamic_cast<SgUntypedBlockDataDeclaration*>(node);
         SgProcedureHeaderStatement* sg_function = pConverter->convertSgUntypedBlockDataDeclaration(ut_block_data, currentScope);
         currentScope = sg_function->get_definition()->get_body();
         break;
      }
    case V_SgUntypedVariableDeclaration:
      {
         SgUntypedVariableDeclaration* ut_decl = dynamic_cast<SgUntypedVariableDeclaration*>(node);
         pConverter->convertSgUntypedVariableDeclaration(ut_decl, currentScope);
         break;
      }
    case V_SgUntypedImplicitDeclaration:
      {
         SgUntypedImplicitDeclaration* ut_decl = dynamic_cast<SgUntypedImplicitDeclaration*>(node);
         pConverter->convertSgUntypedImplicitDeclaration(ut_decl, currentScope);
         break;
      }
    case V_SgUntypedNameListDeclaration:
      {
         SgUntypedNameListDeclaration* ut_decl = dynamic_cast<SgUntypedNameListDeclaration*>(node);
         cout << "NEED to convert name list decl\n";
         pConverter->convertSgUntypedNameListDeclaration(ut_decl, currentScope);
         break;
      }
    case V_SgUntypedNullStatement:
      {
         SgUntypedNullStatement* ut_decl = dynamic_cast<SgUntypedNullStatement*>(node);
         pConverter->convertSgUntypedNullStatement(ut_decl, currentScope);
         break;
      }
    default:
      {
#if DEBUG_UNTYPED_TRAVERSAL
        cout << "Down traverse: found a node of type ... " << node->class_name() << ": " << node << endl;
#endif
      }
   }

   return currentScope;
}


SynthesizedAttribute
UntypedTraversal::evaluateSynthesizedAttribute(SgNode* node, InheritedAttribute currentScope, SynthesizedAttributesList childAttrs)
{
// Synthesized attribute is an expression initialized to NULL for when an expression is a statement.
// Statements are added to the scope and don't need to be returned as an attribute.
   SynthesizedAttribute sg_expr = NULL;   

   switch (node->variantT())
   {
     case V_SgUntypedUnaryOperator:
       {
          SgUntypedExpression* ut_expr = isSgUntypedUnaryOperator(node);
          SgExpressionPtrList children(childAttrs);
          sg_expr = pConverter->convertSgUntypedExpression(ut_expr, children);
          break;
       }

     case V_SgUntypedBinaryOperator:
       {
          SgUntypedExpression* ut_expr = isSgUntypedBinaryOperator(node);
          SgExpressionPtrList children(childAttrs);
          sg_expr = pConverter->convertSgUntypedExpression(ut_expr, children);
          break;
       }

     case V_SgUntypedNullExpression:
     case V_SgUntypedReferenceExpression:
     case V_SgUntypedValueExpression:
       {
          SgUntypedExpression* ut_expr = isSgUntypedExpression(node);
          sg_expr = pConverter->convertSgUntypedExpression(ut_expr);
          break;
       }

    case V_SgUntypedAssignmentStatement:
      {
         SgUntypedAssignmentStatement* ut_stmt = dynamic_cast<SgUntypedAssignmentStatement*>(node);
         SgExpressionPtrList children(childAttrs);
         pConverter->convertSgUntypedAssignmentStatement(ut_stmt, children, currentScope);
         break;
      }
    case V_SgUntypedExpressionStatement:
      {
         SgUntypedExpressionStatement* ut_stmt = dynamic_cast<SgUntypedExpressionStatement*>(node);
         SgExpressionPtrList children(childAttrs);
         pConverter->convertSgUntypedExpressionStatement(ut_stmt, children, currentScope);
         break;
      }
    case V_SgUntypedOtherStatement:
      {
         SgUntypedOtherStatement* ut_stmt = dynamic_cast<SgUntypedOtherStatement*>(node);
         pConverter->convertSgUntypedOtherStatement(ut_stmt, currentScope);
         break;
      }

    default:
      {
#if DEBUG_UNTYPED_TRAVERSAL
         cout << "Up   traverse: found a node of type ... " << node->class_name() << ": " << node << endl;
#endif
      }
   }

// Make sure that the untyped node has been deleted as it is no longer needed
// Note that SgUntypedFile nodes are constructed and deleted by the ATerm traversal class
   if (node != NULL && !isSgUntypedFile(node))
      {
#if DEBUG_UNTYPED_TRAVERSAL
         cout << "-----traverse: deleting node of type ... " << node->class_name() << ": " << node << endl;
#endif
         delete node;
      }
   
   return sg_expr;
}
