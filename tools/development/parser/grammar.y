%{
/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * This is imported from GStreamer and altered to parse GST-Pipeline
 */

#include <glib-object.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"

/* All error messages in this file are user-visible and need to be translated.
 * Don't start the message with a capital, and don't end them with a period,
 * as they will be presented inside a sentence/error.
 */

#define YYERROR_VERBOSE 1

#define YYENABLE_NLS 0

#ifndef YYLTYPE_IS_TRIVIAL
#define YYLTYPE_IS_TRIVIAL 0
#endif

/*******************************************************************************************
*** define SET_ERROR macro/function
*******************************************************************************************/
#ifdef G_HAVE_ISO_VARARGS

#  define SET_ERROR(error, type, ...) \
G_STMT_START { \
  g_printerr (__VA_ARGS__); \
  if ((error) && !*(error)) { \
    g_set_error ((error), GST2PBTXT_PARSE_ERROR, (type), __VA_ARGS__); \
  } \
} G_STMT_END

#elif defined(G_HAVE_GNUC_VARARGS)

#  define SET_ERROR(error, type, args...) \
G_STMT_START { \
  g_printerr (args ); \
  if ((error) && !*(error)) { \
    g_set_error ((error), GST2PBTXT_PARSE_ERROR, (type), args ); \
  } \
} G_STMT_END

#else

static inline void
SET_ERROR (GError **error, gint type, const char *format, ...)
{
  if (error) {
    if (*error) {
      g_warning ("error while parsing");
    } else {
      va_list varargs;
      char *string;

      va_start (varargs, format);
      string = g_strdup_vprintf (format, varargs);
      va_end (varargs);

      g_set_error (error, GST2PBTXT_PARSE_ERROR, type, string);

      g_free (string);
    }
  }
}

#endif /* G_HAVE_ISO_VARARGS */

/*** define YYPRINTF macro/function if we're debugging */

/* bison 1.35 calls this macro with side effects, we need to make sure the
   side effects work - crappy bison */

#ifndef GST_DISABLE_GST_DEBUG
#  define YYDEBUG 1

#  ifdef G_HAVE_ISO_VARARGS

/* #  define YYFPRINTF(a, ...) g_printerr (__VA_ARGS__) */
#    define YYFPRINTF(a, ...) \
G_STMT_START { \
     g_print (__VA_ARGS__); \
} G_STMT_END

#  elif defined(G_HAVE_GNUC_VARARGS)

#    define YYFPRINTF(a, args...) \
G_STMT_START { \
     g_print (args); \
} G_STMT_END

#  else

static inline void
YYPRINTF(const char *format, ...)
{
  va_list varargs;
  gchar *temp;

  va_start (varargs, format);
  temp = g_strdup_vprintf (format, varargs);
  g_print ("%s", temp);
  g_free (temp);
  va_end (varargs);
}

#  endif /* G_HAVE_ISO_VARARGS */

#endif /* GST_DISABLE_GST_DEBUG */


/*
 * include headers generated by bison & flex, after defining (or not defining) YYDEBUG
 */
#include "grammar.tab.h"
#include "parse_lex.h"

/*******************************************************************************************
*** report missing elements/bins/..
*******************************************************************************************/


static void  add_missing_element(graph_t *graph,gchar *name){
  if ((graph)->ctx){
    (graph)->ctx->missing_elements = g_list_append ((graph)->ctx->missing_elements, g_strdup (name));
    }
}


/*******************************************************************************************
*** helpers for pipeline-setup
*******************************************************************************************/

#define TRY_SETUP_LINK(l) G_STMT_START { \
   if( (!(l)->src.element) && (!(l)->src.name) ){ \
     SET_ERROR (graph->error, GST2PBTXT_PARSE_ERROR_LINK, _("link has no source [sink=%s@%p]"), \
	(l)->sink.name ? (l)->sink.name : "", \
	(l)->sink.element); \
     gst_parse_free_link (l); \
   }else if( (!(l)->sink.element) && (!(l)->sink.name) ){ \
     SET_ERROR (graph->error, GST2PBTXT_PARSE_ERROR_LINK, _("link has no sink [source=%s@%p]"), \
	(l)->src.name ? (l)->src.name : "", \
	(l)->src.element); \
     gst_parse_free_link (l); \
   }else{ \
     graph->links = g_slist_append (graph->links, l ); \
   }   \
} G_STMT_END

typedef struct {
  gchar *src_pad;
  gchar *sink_pad;
  GstElement *sink;
  GstCaps *caps;
  gulong pad_added_signal_id, no_more_pads_signal_id;
  gboolean all_pads;
} DelayedLink;

typedef struct {
  gchar *name;
  gchar *value_str;
  gulong signal_id;
} DelayedSet;

static int  gst_resolve_reference(reference_t *rr, GstElement *pipeline){
  GstBin *bin;

  if(rr->element) return  0;  /* already resolved! */
  if(!rr->name)   return -2;  /* no chance! */

  if (GST_IS_BIN (pipeline)){
    bin = GST_BIN (pipeline);
    rr->element = gst_bin_get_by_name_recurse_up (bin, rr->name);
  } else {
    rr->element = strcmp (GST_ELEMENT_NAME (pipeline), rr->name) == 0 ?
		gst_object_ref(pipeline) : NULL;
  }
  if(rr->element) return 0; /* resolved */
  else            return -1; /* not found */
}

static void gst_parse_free_delayed_set (DelayedSet *set)
{
  g_free(set->name);
  g_free(set->value_str);
  g_slice_free(DelayedSet, set);
}

static void gst_parse_new_child(GstChildProxy *child_proxy, GObject *object,
    const gchar * name, gpointer data);

static void gst_parse_add_delayed_set (GstElement *element, gchar *name, gchar *value_str)
{
  DelayedSet *data = g_slice_new0 (DelayedSet);

  GST_CAT_LOG_OBJECT (GST_CAT_PIPELINE, element, "delaying property set %s to %s",
    name, value_str);

  data->name = g_strdup(name);
  data->value_str = g_strdup(value_str);
  data->signal_id = g_signal_connect_data(element, "child-added",
      G_CALLBACK (gst_parse_new_child), data, (GClosureNotify)
      gst_parse_free_delayed_set, (GConnectFlags) 0);

  /* FIXME: we would need to listen on all intermediate bins too */
  if (GST_IS_BIN (element)) {
    gchar **names, **current;
    GstElement *parent, *child;

    current = names = g_strsplit (name, "::", -1);
    parent = gst_bin_get_by_name (GST_BIN_CAST (element), current[0]);
    current++;
    while (parent && current[0]) {
      child = gst_bin_get_by_name (GST_BIN (parent), current[0]);
      if (!child && current[1]) {
        char *sub_name = g_strjoinv ("::", &current[0]);

        gst_parse_add_delayed_set(parent, sub_name, value_str);
        g_free (sub_name);
      }
      gst_object_unref (parent);
      parent = child;
      current++;
    }
    if (parent)
      gst_object_unref (parent);
    g_strfreev (names);
  }
}

static void gst_parse_new_child(GstChildProxy *child_proxy, GObject *object,
    const gchar * name, gpointer data)
{
  DelayedSet *set = (DelayedSet *) data;
  GParamSpec *pspec;
  GValue v = { 0, };
  GObject *target = NULL;
  GType value_type;

  GST_CAT_LOG_OBJECT (GST_CAT_PIPELINE, child_proxy, "new child %s, checking property %s",
      name, set->name);

  if (gst_child_proxy_lookup (child_proxy, set->name, &target, &pspec)) {
    gboolean got_value = FALSE;

    value_type = pspec->value_type;

    GST_CAT_LOG_OBJECT (GST_CAT_PIPELINE, child_proxy, "parsing delayed property %s as a %s from %s",
      pspec->name, g_type_name (value_type), set->value_str);
    g_value_init (&v, value_type);
    if (gst_value_deserialize (&v, set->value_str))
      got_value = TRUE;
    else if (g_type_is_a (value_type, GST_TYPE_ELEMENT)) {
       GstElement *bin;

       bin = gst_parse_bin_from_description_full (set->value_str, TRUE, NULL,
           GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS | GST_PARSE_FLAG_PLACE_IN_BIN, NULL);
       if (bin) {
         g_value_set_object (&v, bin);
         got_value = TRUE;
       }
    }
    g_signal_handler_disconnect (child_proxy, set->signal_id);
    if (!got_value)
      goto error;
    g_object_set_property (target, pspec->name, &v);
  } else {
    const gchar *obj_name = GST_OBJECT_NAME(object);
    gint len = strlen (obj_name);

    /* do a delayed set */
    if ((strlen (set->name) > (len + 2)) && !strncmp (set->name, obj_name, len) && !strncmp (&set->name[len], "::", 2)) {
      gst_parse_add_delayed_set (GST_ELEMENT(child_proxy), set->name, set->value_str);
    }
  }

out:
  if (G_IS_VALUE (&v))
    g_value_unset (&v);
  if (target)
    g_object_unref (target);
  return;

error:
  GST_CAT_ERROR (GST_CAT_PIPELINE, "could not set property \"%s\" in %"
      GST_PTR_FORMAT, pspec->name, target);
  goto out;
}

static void nnstparser_element_set (gchar *value, _Element *element, graph_t *graph)
{
  gchar *pos = value;
  _Property *prop;

  /* do nothing if assignment is for missing element */
  if (element == NULL)
    goto out;

  /* parse the string, so the property name is null-terminated and pos points
     to the beginning of the value */
  while (!g_ascii_isspace (*pos) && (*pos != '=')) pos++;
  if (*pos == '=') {
    *pos = '\0';
  } else {
    *pos = '\0';
    pos++;
    while (g_ascii_isspace (*pos)) pos++;
  }
  pos++;
  while (g_ascii_isspace (*pos)) pos++;
  /* truncate a string if it is delimited with double quotes */
  if (*pos == '"' && pos[strlen (pos) - 1] == '"') {
    pos++;
    pos[strlen (pos) - 1] = '\0';
  }
  gst_parse_unescape (pos);

  /* Assign a "name=value" pair to element */
  prop = g_malloc (sizeof(_Property));
  prop->name = g_strdup (value);
  prop->value = g_strdup (pos);
  element->properties = g_slist_prepend (element->properties, prop);

out:
  g_free (value);
  return;
}

static void gst_parse_free_reference (reference_t *rr)
{
  if(rr->element) gst_object_unref(rr->element);
  gst_parse_strfree (rr->name);
  g_slist_foreach (rr->pads, (GFunc) gst_parse_strfree, NULL);
  g_slist_free (rr->pads);
}

static void gst_parse_free_link (link_t *link)
{
  gst_parse_free_reference (&(link->src));
  gst_parse_free_reference (&(link->sink));
  if (link->caps) gst_caps_unref (link->caps);
  gst_parse_link_free (link);
}

static void gst_parse_free_chain (chain_t *ch)
{
  GSList *walk;
  gst_parse_free_reference (&(ch->first));
  gst_parse_free_reference (&(ch->last));
  for(walk=ch->elements;walk;walk=walk->next)
    gst_object_unref (walk->data);
  g_slist_free (ch->elements);
  gst_parse_chain_free (ch);
}

static void gst_parse_free_delayed_link (DelayedLink *link)
{
  g_free (link->src_pad);
  g_free (link->sink_pad);
  if (link->caps) gst_caps_unref (link->caps);
  g_slice_free (DelayedLink, link);
}

#define PRETTY_PAD_NAME_FMT "%s %s of %s named %s"
#define PRETTY_PAD_NAME_ARGS(elem, pad_name) \
  (pad_name ? "pad " : "some"), (pad_name ? pad_name : "pad"), \
  G_OBJECT_TYPE_NAME(elem), GST_STR_NULL (GST_ELEMENT_NAME (elem))

static void gst_parse_no_more_pads (GstElement *src, gpointer data)
{
  DelayedLink *link = data;

  /* Don't warn for all-pads links, as we expect those to
   * still be active at no-more-pads */
  if (!link->all_pads) {
    GST_ELEMENT_WARNING(src, PARSE, DELAYED_LINK,
      (_("Delayed linking failed.")),
      ("failed delayed linking " PRETTY_PAD_NAME_FMT " to " PRETTY_PAD_NAME_FMT,
          PRETTY_PAD_NAME_ARGS (src, link->src_pad),
          PRETTY_PAD_NAME_ARGS (link->sink, link->sink_pad)));
  }
  /* we keep the handlers connected, so that in case an element still adds a pad
   * despite no-more-pads, we will consider it for pending delayed links */
}

static void gst_parse_found_pad (GstElement *src, GstPad *pad, gpointer data)
{
  DelayedLink *link = data;

  GST_CAT_INFO (GST_CAT_PIPELINE,
                "trying delayed linking %s " PRETTY_PAD_NAME_FMT " to " PRETTY_PAD_NAME_FMT,
		            link->all_pads ? "all pads" : "one pad",
                PRETTY_PAD_NAME_ARGS (src, link->src_pad),
                PRETTY_PAD_NAME_ARGS (link->sink, link->sink_pad));

  if (gst_element_link_pads_filtered (src, link->src_pad, link->sink,
      link->sink_pad, link->caps)) {
    /* do this here, we don't want to get any problems later on when
     * unlocking states */
    GST_CAT_DEBUG (GST_CAT_PIPELINE,
                   "delayed linking %s " PRETTY_PAD_NAME_FMT " to " PRETTY_PAD_NAME_FMT " worked",
		               link->all_pads ? "all pads" : "one pad",
                   PRETTY_PAD_NAME_ARGS (src, link->src_pad),
                   PRETTY_PAD_NAME_ARGS (link->sink, link->sink_pad));
    g_signal_handler_disconnect (src, link->no_more_pads_signal_id);
    /* releases 'link' */
    if (!link->all_pads)
      g_signal_handler_disconnect (src, link->pad_added_signal_id);
  }
}

/* both padnames and the caps may be NULL */
static gboolean
gst_parse_perform_delayed_link (GstElement *src, const gchar *src_pad,
                                GstElement *sink, const gchar *sink_pad,
                                GstCaps *caps, gboolean all_pads)
{
  GList *templs = gst_element_class_get_pad_template_list (
      GST_ELEMENT_GET_CLASS (src));

  for (; templs; templs = templs->next) {
    GstPadTemplate *templ = (GstPadTemplate *) templs->data;
    if ((GST_PAD_TEMPLATE_DIRECTION (templ) == GST_PAD_SRC) &&
        (GST_PAD_TEMPLATE_PRESENCE(templ) == GST_PAD_SOMETIMES))
    {
      DelayedLink *data = g_slice_new (DelayedLink);

      data->all_pads = all_pads;

      /* TODO: maybe we should check if src_pad matches this template's names */

      GST_CAT_DEBUG (GST_CAT_PIPELINE,
                     "trying delayed link " PRETTY_PAD_NAME_FMT " to " PRETTY_PAD_NAME_FMT,
                     PRETTY_PAD_NAME_ARGS (src, src_pad),
                     PRETTY_PAD_NAME_ARGS (sink, sink_pad));

      data->src_pad = g_strdup (src_pad);
      data->sink = sink;
      data->sink_pad = g_strdup (sink_pad);
      if (caps) {
        data->caps = gst_caps_copy (caps);
      } else {
        data->caps = NULL;
      }
      data->pad_added_signal_id = g_signal_connect_data (src, "pad-added",
          G_CALLBACK (gst_parse_found_pad), data,
          (GClosureNotify) gst_parse_free_delayed_link, (GConnectFlags) 0);
      data->no_more_pads_signal_id = g_signal_connect (src, "no-more-pads",
          G_CALLBACK (gst_parse_no_more_pads), data);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
gst_parse_element_can_do_caps (GstElement * e, GstPadDirection dir,
    GstCaps * link_caps)
{
  gboolean can_do = FALSE, done = FALSE;
  GstIterator *it;

  it = (dir == GST_PAD_SRC) ? gst_element_iterate_src_pads (e) : gst_element_iterate_sink_pads (e);

  while (!done && !can_do) {
    GValue v = G_VALUE_INIT;
    GstPad *pad;
    GstCaps *caps;

    switch (gst_iterator_next (it, &v)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&v);

        caps = gst_pad_get_current_caps (pad);
        if (caps == NULL)
          caps = gst_pad_query_caps (pad, NULL);

        can_do = gst_caps_can_intersect (caps, link_caps);

        GST_TRACE ("can_do: %d for %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT,
            can_do, caps, link_caps);

        gst_caps_unref (caps);

        g_value_unset (&v);
        break;
      case GST_ITERATOR_DONE:
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (it);
        break;
    }
  }

  gst_iterator_free (it);

  return can_do;
}

/*
 * performs a link and frees the struct. src and sink elements must be given
 * return values   0 - link performed
 *                 1 - link delayed
 *                <0 - error
 */
static gint
gst_parse_perform_link (link_t *link, graph_t *graph)
{
  GstElement *src = link->src.element;
  GstElement *sink = link->sink.element;
  GSList *srcs = link->src.pads;
  GSList *sinks = link->sink.pads;
  g_assert (GST_IS_ELEMENT (src));
  g_assert (GST_IS_ELEMENT (sink));

  GST_CAT_INFO (GST_CAT_PIPELINE,
      "linking " PRETTY_PAD_NAME_FMT " to " PRETTY_PAD_NAME_FMT " (%u/%u) with caps \"%" GST_PTR_FORMAT "\"",
      PRETTY_PAD_NAME_ARGS (src, link->src.name),
      PRETTY_PAD_NAME_ARGS (sink, link->sink.name),
      g_slist_length (srcs), g_slist_length (sinks), link->caps);

  if (!srcs || !sinks) {
    gboolean found_one = gst_element_link_pads_filtered (src,
        srcs ? (const gchar *) srcs->data : NULL, sink,
        sinks ? (const gchar *) sinks->data : NULL, link->caps);

    if (found_one) {
      if (!link->all_pads)
        goto success; /* Linked one, and not an all-pads link = we're done */

      /* Try and link more available pads */
      while (gst_element_link_pads_filtered (src,
        srcs ? (const gchar *) srcs->data : NULL, sink,
        sinks ? (const gchar *) sinks->data : NULL, link->caps));
    }

    /* We either didn't find any static pads, or this is a all-pads link,
     * in which case watch for future pads and link those. Not a failure
     * in the all-pads case if there's no sometimes pads to watch */
    if (gst_parse_perform_delayed_link (src,
          srcs ? (const gchar *) srcs->data : NULL,
          sink, sinks ? (const gchar *) sinks->data : NULL, link->caps,
	  link->all_pads) || link->all_pads) {
      goto success;
    } else {
      goto error;
    }
  }
  if (g_slist_length (link->src.pads) != g_slist_length (link->sink.pads)) {
    goto error;
  }
  while (srcs && sinks) {
    const gchar *src_pad = (const gchar *) srcs->data;
    const gchar *sink_pad = (const gchar *) sinks->data;
    srcs = g_slist_next (srcs);
    sinks = g_slist_next (sinks);
    if (gst_element_link_pads_filtered (src, src_pad, sink, sink_pad,
        link->caps)) {
      continue;
    } else {
      if (gst_parse_perform_delayed_link (src, src_pad,
                                          sink, sink_pad,
					  link->caps, link->all_pads)) {
	continue;
      } else {
        goto error;
      }
    }
  }

success:
  gst_parse_free_link (link);
  return 0;

error:
  if (link->caps != NULL) {
    gboolean src_can_do_caps, sink_can_do_caps;
    gchar *caps_str = gst_caps_to_string (link->caps);

    src_can_do_caps =
        gst_parse_element_can_do_caps (src, GST_PAD_SRC, link->caps);
    sink_can_do_caps =
        gst_parse_element_can_do_caps (sink, GST_PAD_SINK, link->caps);

    if (!src_can_do_caps && sink_can_do_caps) {
      SET_ERROR (graph->error, GST_PARSE_ERROR_LINK,
          _("could not link %s to %s, %s can't handle caps %s"),
          GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink),
          GST_ELEMENT_NAME (src), caps_str);
    } else if (src_can_do_caps && !sink_can_do_caps) {
      SET_ERROR (graph->error, GST_PARSE_ERROR_LINK,
          _("could not link %s to %s, %s can't handle caps %s"),
          GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink),
          GST_ELEMENT_NAME (sink), caps_str);
    } else if (!src_can_do_caps && !sink_can_do_caps) {
      SET_ERROR (graph->error, GST_PARSE_ERROR_LINK,
          _("could not link %s to %s, neither element can handle caps %s"),
          GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink), caps_str);
    } else {
      SET_ERROR (graph->error, GST_PARSE_ERROR_LINK,
          _("could not link %s to %s with caps %s"),
          GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (sink), caps_str);
    }
    g_free (caps_str);
  } else {
    SET_ERROR (graph->error, GST_PARSE_ERROR_LINK,
        _("could not link %s to %s"), GST_ELEMENT_NAME (src),
        GST_ELEMENT_NAME (sink));
  }
  gst_parse_free_link (link);
  return -1;
}


static int yyerror (void *scanner, graph_t *graph, const char *s);
%}

%union {
    gchar *ss;
    chain_t *cc;
    link_t *ll;
    reference_t rr;
    GstElement *ee;
    GSList *pp;
    graph_t *gg;
}

/* No grammar ambiguities expected, FAIL otherwise */
%expect 0

%token <ss> PARSE_URL
%token <ss> IDENTIFIER
%left  <ss> REF PADREF BINREF
%token <ss> ASSIGNMENT
%token <ss> LINK
%token <ss> LINK_ALL

%type <ss> binopener
%type <gg> graph
%type <cc> chain bin chainlist openchain elementary
%type <rr> reference
%type <ll> link
%type <ee> element
%type <pp> morepads pads assignments

%destructor {	gst_parse_strfree ($$);		} <ss>
%destructor {	if($$)
		  gst_parse_free_chain($$);	} <cc>
%destructor {	gst_parse_free_link ($$);	} <ll>
%destructor {	gst_parse_free_reference(&($$));} <rr>
%destructor {	gst_object_unref ($$);		} <ee>
%destructor {	GSList *walk;
		for(walk=$$;walk;walk=walk->next)
		  gst_parse_strfree (walk->data);
		g_slist_free ($$);		} <pp>



%left '(' ')'
%left ','
%right '.'
%left '!' '=' ':'

%lex-param { void *scanner }
%parse-param { void *scanner }
%parse-param { graph_t *graph }
%pure-parser

%start graph
%%

/*************************************************************
* Grammar explanation:
*   _element_s are specified by an identifier of their type.
*   a name can be give in the optional property-assignments
*	coffeeelement
*	fakesrc name=john
*	identity silence=false name=frodo
*   (cont'd)
**************************************************************/
element:	IDENTIFIER     		      { $$ = nnstparser_element_make ($1, NULL);
						if ($$ == NULL) {
						  add_missing_element(graph, $1);
						  SET_ERROR (graph->error, GST_PARSE_ERROR_NO_SUCH_ELEMENT, _("no element \"%s\""), $1);
						}
						g_free ($1);
                                              }
	|	element ASSIGNMENT	      { nnstparser_element_set ($2, $1, graph);
						$$ = $1;
	                                      }
	;

/*************************************************************
* Grammar explanation: (cont'd)
*   a graph has (pure) _element_s, _bin_s and _link_s.
*   since bins are special elements, bins and elements can
*   be generalized as _elementary_.
*   The construction of _bin_s will be discussed later.
*   (cont'd)
*
**************************************************************/
elementary:
	element				      { $$ = g_slice_new0 (chain_t);
						/* g_print ("@%p: CHAINing elementary\n", $$); */
						$$->first.element = $1? ($1) : NULL;
						$$->last.element = $1? ($1) : NULL;
						$$->first.name = $$->last.name = NULL;
						$$->first.pads = $$->last.pads = NULL;
						$$->elements = $1 ? g_slist_prepend (NULL, $1) : NULL;
					      }
	| bin				      { $$=$1; }
	;

/*************************************************************
* Grammar explanation: (cont'd)
*   a _chain_ is a list of _elementary_s that have _link_s inbetween
*   which are represented through infix-notation.
*
*	fakesrc ! sometransformation ! fakesink
*
*   every _link_ can be augmented with _pads_.
*
*	coffeesrc .sound ! speakersink
*	multisrc  .movie,ads ! .projector,smallscreen multisink
*
*   and every _link_ can be setup to filter media-types
*	mediasrc ! audio/x-raw, signed=TRUE ! stereosink
*
* User HINT:
*   if the lexer does not recognize your media-type it
*   will make it an element name. that results in errors
*   like
*	NO SUCH ELEMENT: no element audio7x-raw
*   '7' vs. '/' in https://en.wikipedia.org/wiki/QWERTZ
*
* Parsing HINT:
*   in the parser we need to differ between chains that can
*   be extended by more elementaries (_openchain_) and others
*   that are syntactically closed (handled later in this file).
*	(e.g. fakesrc ! sinkreferencename.padname)
**************************************************************/
chain:	openchain			      { $$=$1;
						if($$->last.name){
							SET_ERROR (graph->error, GST_PARSE_ERROR_SYNTAX,
							_("unexpected reference \"%s\" - ignoring"), $$->last.name);
							gst_parse_strfree($$->last.name);
							$$->last.name=NULL;
						}
						if($$->last.pads){
							SET_ERROR (graph->error, GST_PARSE_ERROR_SYNTAX,
							_("unexpected pad-reference \"%s\" - ignoring"), (gchar*)$$->last.pads->data);
							g_slist_foreach ($$->last.pads, (GFunc) gst_parse_strfree, NULL);
							g_slist_free ($$->last.pads);
							$$->last.pads=NULL;
						}
					      }
	;

openchain:
	elementary pads			      { $$=$1;
						$$->last.pads = g_slist_concat ($$->last.pads, $2);
						/* g_print ("@%p@%p: FKI elementary pads\n", $1, $$->last.pads); */
					      }
	| openchain link pads elementary pads
					      {
						$2->src  = $1->last;
						$2->sink = $4->first;
						$2->sink.pads = g_slist_concat ($3, $2->sink.pads);
						TRY_SETUP_LINK($2);
						$4->first = $1->first;
						$4->elements = g_slist_concat ($1->elements, $4->elements);
						gst_parse_chain_free($1);
						$$ = $4;
						$$->last.pads = g_slist_concat ($$->last.pads, $5);
					      }
	;

link:	LINK				      { $$ = gst_parse_link_new ();
						$$->all_pads = FALSE;
						if ($1) {
						  $$->caps = gst_caps_from_string ($1);
						  if ($$->caps == NULL)
						    SET_ERROR (graph->error, GST_PARSE_ERROR_LINK, _("could not parse caps \"%s\""), $1);
						  gst_parse_strfree ($1);
						}
					      }
	| LINK_ALL		              { $$ = gst_parse_link_new ();
						$$->all_pads = TRUE;
						if ($1) {
						  $$->caps = gst_caps_from_string ($1);
						  if ($$->caps == NULL)
						    SET_ERROR (graph->error, GST_PARSE_ERROR_LINK, _("could not parse caps \"%s\""), $1);
						  gst_parse_strfree ($1);
						}
					      }
	;
pads:		/* NOP */		      { $$ = NULL; }
	|	PADREF morepads		      { $$ = $2;
						$$ = g_slist_prepend ($$, $1);
					      }
	;
morepads:	/* NOP */		      { $$ = NULL; }
	|	',' IDENTIFIER morepads	      { $$ = g_slist_prepend ($3, $2); }
	;

/*************************************************************
* Grammar explanation: (cont'd)
*   the first and last elements of a _chain_ can be give
*   as URL. This creates special elements that fit the URL.
*
*	fakesrc ! http://fake-sink.org
*       http://somesource.org ! fakesink
**************************************************************/

chain:	openchain link PARSE_URL	      { GstElement *element =
							  gst_element_make_from_uri (GST_URI_SINK, $3, NULL, NULL);
						/* FIXME: get and parse error properly */
						if (!element) {
						  SET_ERROR (graph->error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
							  _("no sink element for URI \"%s\""), $3);
						}
						$$ = $1;
						$2->sink.element = element?gst_object_ref(element):NULL;
						$2->src = $1->last;
						TRY_SETUP_LINK($2);
						$$->last.element = NULL;
						$$->last.name = NULL;
						$$->last.pads = NULL;
						if(element) $$->elements = g_slist_append ($$->elements, element);
						g_free ($3);
					      }
	;
openchain:
	PARSE_URL			      { GstElement *element =
							  gst_element_make_from_uri (GST_URI_SRC, $1, NULL, NULL);
						/* FIXME: get and parse error properly */
						if (!element) {
						  SET_ERROR (graph->error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
						    _("no source element for URI \"%s\""), $1);
						}
						$$ = gst_parse_chain_new ();
						/* g_print ("@%p: CHAINing srcURL\n", $$); */
						$$->first.element = NULL;
						$$->first.name = NULL;
						$$->first.pads = NULL;
						$$->last.element = element ? gst_object_ref(element):NULL;
						$$->last.name = NULL;
						$$->last.pads = NULL;
						$$->elements = element ? g_slist_prepend (NULL, element)  : NULL;
						g_free($1);
					      }
	;


/*************************************************************
* Grammar explanation: (cont'd)
*   the first and last elements of a _chain_ can be linked
*   to a named _reference_ (with optional pads).
*
*	fakesrc ! nameOfSinkElement.
*	fakesrc ! nameOfSinkElement.Padname
*	fakesrc ! nameOfSinkElement.Padname, anotherPad
*	nameOfSource.Padname ! fakesink
**************************************************************/

chain:	openchain link reference	      { $$ = $1;
						$2->sink= $3;
						$2->src = $1->last;
						TRY_SETUP_LINK($2);
						$$->last.element = NULL;
						$$->last.name = NULL;
						$$->last.pads = NULL;
					      }
	;


openchain:
	reference 			      { $$ = gst_parse_chain_new ();
						$$->last=$1;
						$$->first.element = NULL;
						$$->first.name = NULL;
						$$->first.pads = NULL;
						$$->elements = NULL;
					      }
	;
reference:	REF morepads		      {
						gchar *padname = $1;
						GSList *pads = $2;
						if (padname) {
						  while (*padname != '.') padname++;
						  *padname = '\0';
						  padname++;
						  if (*padname != '\0')
						    pads = g_slist_prepend (pads, gst_parse_strdup (padname));
						}
						$$.element=NULL;
						$$.name=$1;
						$$.pads=pads;
					      }
	;


/*************************************************************
* Grammar explanation: (cont'd)
*   a _chainlist_ is just a list of _chain_s.
*
*   You can specify _link_s with named
*   _reference_ on each side. That
*   works already after the explanations above.
*	someSourceName.Pad ! someSinkName.
*	someSourceName.Pad,anotherPad ! someSinkName.Apad,Bpad
*
*   If a syntax error occurs, the already finished _chain_s
*   and _links_ are kept intact.
*************************************************************/

chainlist: /* NOP */			      { $$ = NULL; }
	| chainlist chain		      { if ($1){
						  gst_parse_free_reference(&($1->last));
						  gst_parse_free_reference(&($2->first));
						  $2->first = $1->first;
						  $2->elements = g_slist_concat ($1->elements, $2->elements);
						  gst_parse_chain_free ($1);
						}
						$$ = $2;
					      }
	| chainlist error		      { $$=$1;
						GST_CAT_DEBUG (GST_CAT_PIPELINE,"trying to recover from syntax error");
						SET_ERROR (graph->error, GST_PARSE_ERROR_SYNTAX, _("syntax error"));
					      }
	;

/*************************************************************
* Grammar explanation: (cont'd)
*   _bins_
*************************************************************/


assignments:	/* NOP */		      { $$ = NULL; }
	|	ASSIGNMENT assignments 	      { $$ = g_slist_prepend ($2, $1); }
	;

binopener:	'('			      { $$ = gst_parse_strdup("bin"); }
	|	BINREF			      { $$ = $1; }
	;
bin:	binopener assignments chainlist ')'   {
						chain_t *chain = $3;
						GSList *walk;
						GstBin *bin = (GstBin *) gst_element_factory_make ($1, NULL);
						if (!chain) {
						  SET_ERROR (graph->error, GST_PARSE_ERROR_EMPTY_BIN,
						    _("specified empty bin \"%s\", not allowed"), $1);
						  chain = gst_parse_chain_new ();
						  chain->first.element = chain->last.element = NULL;
						  chain->first.name    = chain->last.name    = NULL;
						  chain->first.pads    = chain->last.pads    = NULL;
						  chain->elements = NULL;
						}
						if (!bin) {
						  add_missing_element(graph, $1);
						  SET_ERROR (graph->error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
						    _("no bin \"%s\", unpacking elements"), $1);
						  /* clear property-list */
						  g_slist_foreach ($2, (GFunc) gst_parse_strfree, NULL);
						  g_slist_free ($2);
						  $2 = NULL;
						} else {
						  for (walk = chain->elements; walk; walk = walk->next )
						    gst_bin_add (bin, GST_ELEMENT (walk->data));
						  g_slist_free (chain->elements);
						  chain->elements = g_slist_prepend (NULL, bin);
						}
						$$ = chain;
						/* set the properties now
						 * HINT: property-list cleared above, if bin==NULL
						 */
						for (walk = $2; walk; walk = walk->next)
						  gst_parse_element_set ((gchar *) walk->data,
							GST_ELEMENT (bin), graph);
						g_slist_free ($2);
						gst_parse_strfree ($1);
					      }
	;

/*************************************************************
* Grammar explanation: (cont'd)
*   _graph_
*************************************************************/

graph:	chainlist			      { $$ = graph;
						$$->chain = $1;
						if(!$1) {
						  SET_ERROR (graph->error, GST_PARSE_ERROR_EMPTY, _("empty pipeline not allowed"));
						}
					      }
	;

%%


static int
yyerror (void *scanner, graph_t *graph, const char *s)
{
  /* FIXME: This should go into the GError somehow, but how? */
  GST_WARNING ("Error during parsing: %s", s);
  return -1;
}


GstElement *
priv_gst_parse_launch (const gchar *str, GError **error, GstParseContext *ctx,
    GstParseFlags flags)
{
  graph_t g;
  gchar *dstr;
  GSList *walk;
  GstBin *bin = NULL;
  GstElement *ret;
  yyscan_t scanner;

  g_return_val_if_fail (str != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  g.chain = NULL;
  g.links = NULL;
  g.error = error;
  g.ctx = ctx;
  g.flags = flags;

#ifdef __GST_PARSE_TRACE
  GST_CAT_DEBUG (GST_CAT_PIPELINE, "TRACE: tracing enabled");
  __strings = __chains = __links = 0;
#endif /* __GST_PARSE_TRACE */

  /* g_print("Now scanning: %s\n", str); */

  dstr = g_strdup (str);
  priv_gst_parse_yylex_init (&scanner);
  priv_gst_parse_yy_scan_string (dstr, scanner);

#if YYDEBUG
  yydebug = 1;
#endif

  if (yyparse (scanner, &g) != 0) {
    SET_ERROR (error, GST_PARSE_ERROR_SYNTAX,
        "Unrecoverable syntax error while parsing pipeline %s", str);

    priv_gst_parse_yylex_destroy (scanner);
    g_free (dstr);

    goto error1;
  }
  priv_gst_parse_yylex_destroy (scanner);
  g_free (dstr);

  GST_CAT_DEBUG (GST_CAT_PIPELINE, "got %u elements and %u links",
      g.chain ? g_slist_length (g.chain->elements) : 0,
      g_slist_length (g.links));

  /* ensure chain is not NULL */
  if (!g.chain){
    g.chain=g_slice_new0 (chain_t);
    g.chain->elements=NULL;
    g.chain->first.element=NULL;
    g.chain->first.name=NULL;
    g.chain->first.pads=NULL;
    g.chain->last.element=NULL;
    g.chain->last.name=NULL;
    g.chain->last.pads=NULL;
  };

  /* ensure elements is not empty */
  if(!g.chain->elements){
    g.chain->elements= g_slist_prepend (NULL, NULL);
  };

  /* put all elements in our bin if necessary */
  if(g.chain->elements->next){
    if (flags & GST_PARSE_FLAG_PLACE_IN_BIN)
      bin = GST_BIN (gst_element_factory_make ("bin", NULL));
    else
      bin = GST_BIN (gst_element_factory_make ("pipeline", NULL));
    g_assert (bin);

    for (walk = g.chain->elements; walk; walk = walk->next) {
      if (walk->data != NULL)
        gst_bin_add (bin, GST_ELEMENT (walk->data));
    }
    g_slist_free (g.chain->elements);
    g.chain->elements = g_slist_prepend (NULL, bin);
  }

  ret = (GstElement *) g.chain->elements->data;
  g_slist_free (g.chain->elements);
  g.chain->elements=NULL;
  if (GST_IS_BIN (ret))
    bin = GST_BIN (ret);
  gst_parse_free_chain (g.chain);
  g.chain = NULL;


  /* resolve and perform links */
  for (walk = g.links; walk; walk = walk->next) {
    link_t *l = (link_t *) walk->data;
    int err;
    err=gst_resolve_reference( &(l->src), ret);
    if (err) {
       if(-1==err){
          SET_ERROR (error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
              "No src-element named \"%s\" - omitting link", l->src.name);
       }else{
          /* probably a missing element which we've handled already */
          SET_ERROR (error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
              "No src-element found - omitting link");
       }
       gst_parse_free_link (l);
       continue;
    }

    err=gst_resolve_reference( &(l->sink), ret);
    if (err) {
       if(-1==err){
          SET_ERROR (error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
              "No sink-element named \"%s\" - omitting link", l->src.name);
       }else{
          /* probably a missing element which we've handled already */
          SET_ERROR (error, GST_PARSE_ERROR_NO_SUCH_ELEMENT,
              "No sink-element found - omitting link");
       }
       gst_parse_free_link (l);
       continue;
    }
    gst_parse_perform_link (l, &g);
  }
  g_slist_free (g.links);

out:
#ifdef __GST_PARSE_TRACE
  GST_CAT_DEBUG (GST_CAT_PIPELINE,
      "TRACE: %u strings, %u chains and %u links left", __strings, __chains,
      __links);
  if (__strings || __chains || __links) {
    g_warning ("TRACE: %u strings, %u chains and %u links left", __strings,
        __chains, __links);
  }
#endif /* __GST_PARSE_TRACE */

  return ret;

error1:
  if (g.chain) {
    gst_parse_free_chain (g.chain);
    g.chain=NULL;
  }

  g_slist_foreach (g.links, (GFunc)gst_parse_free_link, NULL);
  g_slist_free (g.links);

  if (error)
    g_assert (*error);
  ret = NULL;

  goto out;
}
