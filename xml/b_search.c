#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2003-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 * Random useful xml search functions, works with libxml2
 */

#include <libbk.h>
#include <libbkxml.h>



/**
 * Search an xml tree (recursively) for an entity
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global environment
 *	@param node The xml node to start the search (preserve even when restarting)
 *	@param last The XML node where we stopped last time (copy in-out--recurse till you get here before checking more)
 *	@param found The XML node where the target was found (copy-out)
 *	@param findtype The type of element we are searching for (typically XML_ELEMENT_NODE)
 *	@param findname The name of the element we are searching for--any if blank
 *	@param depth Bound the depth of the search to this value (-1 for unlimited).
 *	@param flags BKXML_DONT_FIND_THIS_NODE will cause this node to
 *		not be searched, BKXML_BREADTH will do a breadth first
 *		search instead of the default depth-first-search
 *	@return <i>-1</i> on call failure
 *	@return <i>0</i> if the node could not be found
 *	@return <i>1</i> if the node was found
 */
int bkxml_nodesearch(bk_s B, xmlNodePtr node, xmlNodePtr *last, xmlNodePtr *found, xmlElementType findtype, const u_char *findname, int depth, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  int ret;
  int tmp;

  if (!node || !found)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  /*
   * <TODO>The current mechanism for skipping up to the "last" start
   * point is rather inefficient.  It should be possible to do
   * somewhat better by following the parent links from "last" up to
   * parent of the "node" base, then skipping through children (if
   * "node" appears in the parent chain) or siblings until we reach
   * the ancestor that is either a child or sibling of "node".
   * Performing a temporary limit on the depth of last would help as
   * well. In the case of a breadth-first search, this optimization is
   * a bit trickier, but should still be possible.</TODO>
   */

  /*
   * <TODO>In addition to the comments above, it would probably improve
   * performance significantly to replace the second type of recursion
   * (i.e. when tmp==1) with a non-recursive loop, or at the very least,
   * a tail-recursive call that would allow the optimizer to eliminate the
   * extra stack frame (the BK_RETURN stuff may make that impossible).</TODO>
   */

  // Skip if we haven't yet reached "last" start point, or user says so
  if (!(last && *last) && BK_FLAG_ISCLEAR(flags, BKXML_DONT_FIND_THIS_NODE))
  {						// Search this node
    if (node->type == findtype && (!findname || (node->name && !strcmp(node->name, findname))))
    {						// We have found our target!
      *found = node;
      if (last)
	*last = node;
      BK_RETURN(B, 1);
    }
  }
  else
  {
    BK_FLAG_CLEAR(flags, BKXML_DONT_FIND_THIS_NODE);
  }

  /*
   * We have reached the "last" start point, disable skipping, even for our
   * callers higher up in the recursion stack.
   */
  if (last && *last == node)
    *last = NULL;

  /*
   * This silly loop allows us to have one recursive call, which will either be
   * breadth or depth first depending on the flags
   */
  for (tmp=0;tmp<2;tmp++)
  {
    int tmp_depth = depth;
    xmlNodePtr sub;

    if (tmp == BK_FLAG_ISCLEAR(flags, BKXML_BREADTH))
      sub = node->next;
    else
    {
      if (!tmp_depth)				// we have hit depth limit
	continue;				// break out to for loop

      if (tmp_depth > 0)
	tmp_depth--;				// decrement depth limit

      sub = node->children;
    }
    if (sub && ((ret = bkxml_nodesearch(B, sub, last, found, findtype, findname, tmp_depth, 0)) != 0))
    {
      BK_RETURN(B, ret);			// Propagate success or error
    }
  }

  // Did not find node, backtrack
  BK_RETURN(B, 0);
}



/**
 * Search an XML node for an desired attribute
 *
 *	@param B BAKA Thread/Global state
 *	@param node The XML node to start the search
 *	@param findname The name of the element we are searching for--any if blank
 *	@param flags fun for the future
 *	@return <i>NULL</i> on call failure, search failure
 *	@return <br><i>attr pointer</i> if the attribute was found
 */
xmlAttrPtr bkxml_attrsearch(bk_s B, xmlNodePtr node, const char *findname, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  xmlAttrPtr attr;

  if (!node || !findname)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  for (attr = node->properties; attr; attr = attr->next)
  {
    if (BK_STREQ(attr->name, findname))
      BK_RETURN(B, attr);
  }

  BK_RETURN(B, NULL);
}



/**
 * Copy and return (you must free) the contents of an attribute or a node
 *
 *	@paran B BAKA Thread/Global state
 *	@param doc The XML document header for special entities (not required for normal entities)
 *	@param attrnode The XML attribute or node whose data you wish to copy
 *	@param flags BKXML_MISSING_TEXT_ARE_NULL, BKXML_EXPAND_VARS
 *	@return <i>NULL</i> on call failure
 *	@return <br><i>allocated string</i> on success
 */
char *bkxml_attrnode_data(bk_s B, xmlDocPtr doc, xmlNodePtr attrnode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  char *str;

  if (!attrnode)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (BK_FLAG_ISCLEAR(flags, BKXML_MISSING_TEXT_ARE_NULL) && !attrnode->children)
    BK_RETURN(B, calloc(1, 1));

  if (!(str = (char *)xmlNodeListGetString(doc, attrnode->children, 1)))
  {
    bk_error_printf(B, BK_ERR_ERR, "XML refused to give me the attribute or node contents for %s\n", attrnode->name);
    BK_RETURN(B, NULL);
  }

  if (BK_FLAG_ISSET(flags, BKXML_EXPAND_VARS))
  {
    // passed in string is freed even on failure
    if (!(str = bk_string_expand(B, str, NULL, (const char**)environ, BK_STRING_EXPAND_FREE)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Environment variable expansion failed.\n");
      goto error;
    }
  }

  BK_RETURN(B, str);

 error:
  if (str)
    free(str);

  BK_RETURN(B, NULL);
}



/**
 * Search for an attrnode by name and return its value.
 *
 *	@param B BAKA thread/global state.
 *	@param node The XML node to start the search
 *	@param findname The name of the element we are searching for--any if blank
 *	@param flags fun for the future
 *	@return <i>NULL</i> on call failure, search failure
 *	@return attribute <i>string</i> on success.
 */
char *
bkxml_attrnode_valbyname(bk_s B, xmlNodePtr node, const char *findname, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  xmlAttrPtr attr;

  if (!(attr = bkxml_attrsearch(B, node, findname, 0)))
  {
    bk_error_printf(B, BK_ERR_NOTICE, "Could not locate attribute: %s\n", findname);
    BK_RETURN(B,NULL);
  }

  BK_RETURN(B, bkxml_attrnode_data(B, NULL, (xmlNodePtr)attr, flags));
}



/**
 * Check if next (or this) xml ELEMENT node has given name. Copy out new
 * node location in nodep (if it exists). If *nodep == node and the next
 * node does not match, do *not* alter nodep.
 *
 * This function is a more restrictive version of bkxml_nodesearch() above
 * which allows you to essentially demand "dtd correctness" by always
 * looking for what is supposed to come next rather than just locating all
 * the relevant tags in any order.
 *
 *	@param B BAKA thread/global state.
 *	@param node. Check starts from this node.
 *	@param name. Name to check for.
 *	@param nodep. Optional copyout xmlNodePtr ptr pointing at next node.
 *	@param flags Flags.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 *	@return <i>1</i> if next element node is not name.
 */
int
bkxml_check_next_node_name(bk_s B, xmlNodePtr node, const xmlChar *name, xmlNodePtr *nodep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");

  if (!node || !name)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISCLEAR(flags, BKXML_CHECK_NEXT_NODE_NAME_CHECK_THIS_NODE))
    node = node->next;

  // Find next node
  while (node->type != XML_ELEMENT_NODE)
    node = node->next;

  // Compare
  if (!xmlStrEqual(node->name, name))
  {
    if (nodep)
      *nodep = NULL;
    BK_RETURN(B, 1);
  }

  // Copy out if desired.
  if (nodep)
    *nodep = node;

  BK_RETURN(B, 0);
}
