# QJSP

I was working on a sandboxed libc that links to QuickJS, and I thought ASIO would be a good idea since I'm used to it. But then I figured I'm way too dumb to mess around with classid, finalizers, and manual GC pinning/unpinning.

So I was like, maybe I should just write my own JS toy engine in C++? I started by borrowing QuickJS's core structures — atom, shape, object, context, runtime — and tried to make them a bit more ergonomic for C++ usage. The idea's pretty simple:

* Hold Value objects (no raw pointers) for RAII, keep resources ref-counted, and break cycles with mark-and-sweep.
* Use vtable dispatch on Object::call() so I don't have to deal with manual classid. That's really it.

I've built a couple of stack-based VMs before for some dynamic code-gen in my trading bot, and this time I figured I'd try writing a register VM. Never done that — went with a Lua-style iABC scheme. My opcode design? Totally ad-hoc, not some expert masterpiece. For example, I wanted to make for loops easier, so I just tossed in FOR_IN/OF_START/NEXT opcodes. Hope I don't burn through all 255 slots before I'm done.

For the lexer and parser, I initially wanted to reuse QuickJS's stuff and just follow their parser path while emitting my own opcodes. But yeah, I quickly realized I'm not smart enough to wrap my head around their tricks and quirks — negative tokens, mixing atoms with tokens, etc. So I rewrote them myself and the lexer and parser are also pretty ad-hoc right now. I'll optimize them once I nail down the basic language features. I looked into using oxc, but integration turned out harder than I thought. Yuku's great, and Zig makes generating FFI headers easier, but they're both still heavily in development, so that's also tricky. Might just study their code instead — they seem to have built a ridiculously fast parser.

The goal is to push as much logic into C++ as possible so performance doesn't totally suck. I want to keep things tight with the ASIO event loop — engine sitting at a couple KB, fast enough to spin up on threads.

Architecture: single `Engine` owns everything (atoms, GC, shape cache, builtins, global scope). Each builtin type registers itself via `static setup(Engine*)` that creates its prototype and installs methods. Object creation uses `Engine::get_proto(Builtin::id)` for the fast path. No separate Runtime/Context — Engine merged them both.

Still scratching my head over a decent sparse array though.

