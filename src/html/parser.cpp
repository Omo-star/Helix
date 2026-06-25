#include "html/parser.h"
#include "html/tokenizer.h"
#include <set>
#include <algorithm>
#include <cctype>

static const std::set<std::string> kVoidTags = {
    "area","base","br","col","embed","hr","img","input",
    "link","meta","param","source","track","wbr"
};

// Tags that implicitly close when a sibling of the same kind opens
static const std::set<std::string> kAutoClose = {
    "p","li","dt","dd","tr","td","th","thead","tbody","tfoot",
    "colgroup","option","optgroup"
};

static const std::set<std::string> kClosesParagraph = {
    "address","article","aside","blockquote","div","dl","fieldset",
    "footer","form","h1","h2","h3","h4","h5","h6","header","hr",
    "main","nav","ol","p","pre","section","table","ul"
};

// Elements that also close specific ancestors (not just self).
// <td>/<th> close an open <td>/<th>; <tr> closes <td>/<th> AND <tr>.
static bool implicitlyCloses(const std::string& newTag, const std::string& openTag) {
    if (newTag == "td" || newTag == "th")
        return openTag == "td" || openTag == "th";
    if (newTag == "tr")
        return openTag == "td" || openTag == "th" || openTag == "tr";
    if (newTag == "thead" || newTag == "tbody" || newTag == "tfoot")
        return openTag == "td" || openTag == "th" || openTag == "tr"
            || openTag == "thead" || openTag == "tbody" || openTag == "tfoot";
    if (newTag == "li") return openTag == "li";
    if (newTag == "dt" || newTag == "dd") return openTag == "dt" || openTag == "dd";
    if (newTag == "option") return openTag == "option";
    if (newTag == "optgroup") return openTag == "option" || openTag == "optgroup";
    return false;
}

// Foreign content tags: their raw content is passed through as a single text node.
static const std::set<std::string> kForeignTags = { "svg", "math" };

std::shared_ptr<Node> ParseHtml(const std::string& html) {
    auto doc = Node::makeDocument();

    // Open-element stack: doc is always at index 0
    std::vector<std::shared_ptr<Node>> stack;
    stack.push_back(doc);

    auto current = [&]() -> Node* { return stack.back().get(); };

    HtmlTokenizer tok;
    tok.tokenize(html, [&](const HtmlToken& t) {
        switch (t.type) {

        case TokenType::StartTag: {
            // HTML permits omitted </p> before many block/table starts.
            if (kClosesParagraph.count(t.name) && stack.size() > 1) {
                if (stack.back()->tagName == "p")
                    stack.pop_back();
            }

            // Implicit closing: pop open elements that the new tag closes.
            // E.g. <td> closes a previous open <td>/<th>, <tr> closes <td>/<th>/<tr>.
            while (stack.size() > 1 && implicitlyCloses(t.name, stack.back()->tagName))
                stack.pop_back();

            auto node = Node::makeElement(t.name);
            node->attrs = t.attrs;
            current()->appendChild(node);

            // <template>: parsed into the DOM but its children are inert
            // (display:none is applied by the layout engine via UA defaults).
            // We still push it on the stack so children attach to it.

            if (!kVoidTags.count(t.name) && !t.selfClosing)
                stack.push_back(node);
            break;
        }

        case TokenType::EndTag: {
            // Walk stack backwards to find matching open tag
            for (int i = (int)stack.size() - 1; i >= 1; --i) {
                if (stack[i]->tagName == t.name) {
                    stack.erase(stack.begin() + i, stack.end());
                    break;
                }
            }
            break;
        }

        case TokenType::Text: {
            // Collapse runs of whitespace; drop all-whitespace nodes
            // inside block elements (they're invisible anyway)
            std::string txt = t.data;
            // Normalise whitespace: newlines/tabs → spaces, runs → single space
            for (auto& c : txt)
                if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            // Collapse consecutive spaces
            std::string collapsed;
            bool lastSpace = false;
            for (char c : txt) {
                if (c == ' ') {
                    if (!lastSpace) collapsed += ' ';
                    lastSpace = true;
                } else {
                    collapsed += c;
                    lastSpace = false;
                }
            }
            if (!collapsed.empty() && collapsed != " ")
                current()->appendChild(Node::makeText(collapsed));
            break;
        }

        default: break;
        }
    });

    return doc;
}
