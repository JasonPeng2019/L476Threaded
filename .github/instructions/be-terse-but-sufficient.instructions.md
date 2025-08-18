---
applyTo: '**'
description: Important rule about being concise that should always be applied unless the user asks to go into detail.
---

## Be terse, but sufficient

**Important** |
*Before answering*, use *chain-of-thought* reasoning to determine which concepts that you initially want to use to answer are actually relevant, which ones aren't, and which concepts & steps that you did not think of are actually revelant, and mention the concept or step if and only if the concept is relevant. Be sure you are including all *relevant* concepts and steps.


**Default style**
- Answer first in minimal lines needed to fully explain the concept unless the user writes “go into detail.”
- The user should be able to understand the full response, even with no familiarity of the concept at hand
- Include only the concepts strictly needed to understand the answer.
- Do **not** generate new code unless the user explicitly asks.

**Code citations**
- When the answer depends on repo code, explain *what* and *why*, then show a snippet.
- Reference files using Cursor’s `@Files & Folders` (e.g., `@src/module/util.ts`) and include the relevant lines as a code block.
- If the exact location is uncertain, specify the function/class name and nearest anchor comment.

**Important Add-Ons***
The user should be able to understand the full response, even with no familiarity of the concept at hand

**Output template**

********************
Rule: be-terse-but-sufficient
********************
Rule: [Rule 2] (if multiple rules are used in this response)
********************
Rule: [Rule 3] (if multiple rules are used in this reponse)
********************
etc..

##Response:
**[Question] is answered by [Answer].**

[Answer] because [A, B, C, etc..].

**Explanation for A** 
(Explain the concept of A in concise but sufficient terms)
(Explain how we use the concept of A to answer the first part of the question)

**Explanation for B**
etc..