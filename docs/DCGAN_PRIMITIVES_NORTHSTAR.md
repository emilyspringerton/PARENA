# DCGAN Primitives for PARENA — Scoping Pass (S210-01)

## Where this comes from

Founder real-time (2026-08-30): "add DCGAN primitives to PARENA." Routed through `emily observe`
before acting (BACKLOG.md SECTION 210). Deliberately not scoped further at the time — a real
DCGAN implementation is large enough that guessing at a plan without first auditing
`stdlib/nn.prn`'s own real, current scope would have meant re-deriving from scratch, or worse,
duplicating work that already exists. This doc is that audit, plus the real, phased plan it was
withheld pending.

## Status

Scoping only. No code changes follow from this doc alone — matching this repo's own established
"Spec Before Implementation" precedent (`NORTHSTAR.md`/`GRAMMAR.md`, and `S223-02`'s own
predecessor scoping doc, `EXPR_POSITION_BINDING_FORMS_NORTHSTAR.md`, which named a plan before any
code landed).

## Part 1 — `stdlib/nn.prn`'s real, current scope (checked directly, not assumed)

`nn.prn` is real, small (165 lines), and **fully working** — every function in it is gcc-clean
under `-pedantic -Werror` and numerically verified (STDLIB.md's own "nn" section, 2026-08-21).
But it was built for exactly one real purpose: **porting `gpt2-alpine-c`'s own inference-time
forward pass** (a pretrained-weights transformer doing text generation), not training anything.
Its real, current inventory:

- `layernorm`, `gelu`, `softmax` — the exact three primitives `gpt2.c`'s own
  `gpt2_model_forward` calls by name.
- `relu`, `sigmoid`, `tanh-activation`, `leaky-relu` — the standard activation trio (plus
  leaky-relu) added as a direct follow-up ask, not GPT-2-specific.
- `map-elementwise` — the one real, shared primitive most of the above reduce to.
- `exp-of`/`tanh-of` — real `#target` FFI wraps of libm, since a real `exp`/`tanh` implementation
  is genuine numerical work this stdlib deliberately doesn't hand-roll.

**What genuinely does NOT exist anywhere in this stdlib, checked directly:**

1. **No 2-D convolution** (`Conv2D`) — `array.prn`'s `NDArray` is real and N-D-capable in
   principle (flat data + strides), but nothing anywhere computes a real convolution over it;
   every real op today (`add`/`mul-elementwise`/the `nn.prn` activations) is elementwise, and
   `linalg.prn`'s `matmul` (now verified correct, 2026-09-08) is 2-D dense matrix multiply, not
   convolution.
2. **No transposed/fractionally-strided convolution** (`ConvTranspose2D`) — DCGAN's own real
   generator upsamples via this operation specifically; nothing here does anything like it.
3. **No `BatchNorm`** — DCGAN's own real, standard generator/discriminator architecture uses
   `BatchNorm2D` between every conv layer except the first/last; `layernorm` above is a different,
   real, unrelated normalization (per-token, not per-channel-across-batch).
4. **No autodiff and no hand-derived backward pass for ANYTHING** — every real function in this
   stdlib is forward-only. There is no computational graph, no gradient type, no `.backward()`
   equivalent, no chain-rule composition of any kind, for even the simplest existing op
   (`relu`'s own derivative, `matmul`'s own two backward formulas, none of it exists).
5. **No optimizer** — no SGD, no Adam (the real, standard DCGAN optimizer), no parameter-update
   primitive of any kind.
6. **No loss function** — no cross-entropy, no BCE (DCGAN's own real, standard discriminator
   loss), nothing.
7. **No random-number generation for a latent vector** — `math/random-f64` exists (this
   session's own earlier work, real and working) and is real, general-purpose-enough to sample a
   DCGAN's own real Gaussian/uniform latent noise vector from, once the rest of this exists.
8. **No image data loading/preprocessing** — no PNG/JPEG decode, no real dataset iteration
   primitive anywhere in this stdlib.

**The real, honest gap size, stated plainly**: a working DCGAN needs items 1-6 above, at minimum,
before a single real training step can run. This is not a "few functions" gap — it is a genuinely
new, large subsystem (a minimal deep-learning framework with real backward-pass support), on the
scale of `linalg.prn`+`array.prn`+`stats.prn` combined, not a `nn.prn` extension.

## Part 2 — The real, single biggest fork in the road: how does PARENA do backpropagation?

Every other gap above (conv2d, conv2d-transpose, batchnorm, optimizer, loss) is "just" real,
well-understood forward-pass math, buildable the same short-formula way everything in this
stdlib already is. Backpropagation is categorically different, and the decision made here
determines the shape of every other phase below. Two real options:

- **Option A — full reverse-mode autodiff.** A real, general computational graph: every op
  records itself and its inputs, a `.backward()` walk applies the chain rule generically. This is
  the real, standard approach modern frameworks (PyTorch, JAX) use, and it's the only approach
  that scales past a fixed, small set of hand-differentiated ops. It is also, honestly, a
  substantial new piece of compiler-adjacent infrastructure in its own right — PARENA has no
  generic dispatch/polymorphism today (confirmed repeatedly this session: `vec.prn`/`map.prn`
  don't compile because VS0 has no real generic monomorphization), so a real "any op, generically
  differentiable" graph needs either a big, new interpretation layer, or hand-written
  boilerplate per op that's arguably no simpler than Option B.
- **Option B — hand-derived backward formulas, per op, matching this stdlib's own established
  style.** Every forward function (`conv2d`, `conv2d-transpose`, `batchnorm2d`, the existing
  activations) gets a real, separate, hand-written `*-backward` sibling computing that specific
  op's own real gradient formula — the same "short, real, hand-rolled formula, not a competing
  general framework" discipline `linalg.prn`'s own header comment already states outright
  ("matmul/transpose/dot exist here as the reference semantics... not a competing fast
  implementation"). A real DCGAN training loop then manually chains these backward calls in the
  exact reverse order of the real forward pass — genuinely more code (every op is two functions,
  not one) but each individual function stays exactly as tractable as everything already in this
  file, no new architecture needed.

**Real, decisive recommendation, not left open the way the prior GAN doc left everything open**:
**Option B.** It matches this stdlib's own entire established philosophy (real, short, honest,
hand-rolled — `array.prn`'s own header comment: "Real, complete implementations... short,
standard formulas, not deferred") and needs zero new compiler capability, unlike Option A, which
would need generic dispatch this compiler has never had and isn't otherwise on a path toward. The
real cost is a from-scratch DCGAN training script this doesn't generalize to arbitrary future
architectures — an honest, accepted tradeoff, not a hidden one — but a fixed, small,
well-understood architecture (DCGAN's own generator/discriminator, a handful of real layer types)
is exactly the case where that tradeoff is cheap.

## Part 3 — Real, phased plan

Each phase is independently real and testable the same way every other file in this stdlib is
(`make test-*`, gcc-verified, numerically checked against hand-computed or reference values) —
not a speculative sketch.

### Phase 0 — `conv/conv2d.prn`: real forward Conv2D

The one real, foundational primitive nothing else can be built on top of without. Real,
standard implementation approach: **im2col + matmul** — unfold each convolution window into a
row of a real 2-D matrix, then a single real `linalg/matmul` (now verified correct) does the
actual convolution as one dense multiply. This is the real, standard technique every real CPU
conv implementation uses when a dedicated conv kernel isn't worth hand-writing, and it means
Phase 0's own real, new code is "build the unfolded matrix," not "reimplement matmul." Real,
honest v0 scope: stride and padding as explicit parameters (no dilation, no groups — real,
separate, later refinements DCGAN's own standard architecture doesn't need).

### Phase 1 — `conv/conv2d_transpose.prn`: real forward ConvTranspose2D

DCGAN's own generator upsampling operation. Real, standard implementation: the transpose of
Phase 0's own im2col — scatter-add each input value's own contribution across the output
window, the literal mathematical transpose of "gather a window into a row." Depends on Phase 0
only for the shared conv-arithmetic helpers (output-size formulas), not its own code.

### Phase 2 — `nn/batchnorm.prn`: real forward BatchNorm2D

Real, standard formula: per-channel mean/variance across the batch, normalize, scale+shift by
learned `gamma`/`beta` — structurally similar to `nn.prn`'s own existing `layernorm` (which
already computes a real mean/variance/normalize), just batched across the N/H/W dimensions
instead of per-token. Real, honest v0: inference-mode only at first (a fixed, pre-computed
running mean/variance, no online statistics tracking) — training-mode batch statistics are a
real, separate, later addition once Phase 4 (backward pass) exists to actually need them.

### Phase 3 — the real backward-pass layer (Option B, per Part 2)

For each of Phase 0/1/2's own forward ops, plus the existing `relu`/`leaky-relu`/`sigmoid`/`tanh`
activations already in `nn.prn`: a real, separate `*-backward` function computing that op's own
hand-derived gradient with respect to its own real inputs. Real, honest, ordered scope (easiest,
best-understood gradients first): activation backward passes (trivial, closed-form derivatives)
→ `matmul-backward` (two real, standard formulas: `dA = dC @ B^T`, `dB = A^T @ dC`) →
`conv2d-backward`/`conv2d-transpose-backward` (real, but genuinely more involved — the same
im2col trick applies in reverse) → `batchnorm-backward` (the real, standard, slightly fiddly
batch-statistics gradient formula every real deep-learning textbook derives).

### Phase 4 — `nn/optim.prn` + `nn/loss.prn`: real Adam optimizer + real BCE loss

Adam (the real, standard DCGAN optimizer choice) needs, per parameter tensor: a real running
first-moment estimate, a real running second-moment estimate, and the real, standard bias-
corrected update formula — genuinely just more elementwise `NDArray` math, no new architecture.
Binary cross-entropy (the real, standard DCGAN discriminator/generator loss) is a real, short,
closed-form formula plus its own real, simple backward gradient.

### Phase 5 — the real DCGAN architecture + training loop, as a real, standalone consumer

Not new stdlib primitives — a real `.prn` program (or a real C host driver calling into PARENA-
compiled layers, matching this whole monorepo's own "PARENA decides, host owns the platform"
split) that: samples a real latent noise vector (`math/random-f64`, already real and working),
chains the real generator (`ConvTranspose2D` → `BatchNorm2D` → `ReLU`, repeated, → `Tanh` output,
DCGAN's own real, standard architecture) and discriminator (`Conv2D` → `BatchNorm2D` →
`LeakyReLU`, repeated, → `Sigmoid` output) forward passes, computes the real BCE loss, walks the
real backward chain built in Phase 3, and applies the real Adam update from Phase 4 — alternating
discriminator and generator updates, DCGAN's own real, standard training procedure.

### Phase 6 — real image data loading (a real, separate, deliberately-last dependency)

DCGAN needs real training images. No PNG/JPEG decode exists anywhere in this stdlib today — a
real, separate, later piece of work (a real PNG decoder, or an FFI wrap of a real, existing C
library, matching this stdlib's own established `#target`-escape-hatch convention for genuine,
separate numerical/format work like `exp-of`/`tanh-of` above). Deliberately sequenced last: every
earlier phase is independently testable against small, synthetic, hand-constructed tensors (the
same "hand-traced values, not guessed" discipline this whole session's own test suite already
uses) without needing any real image data at all.

## Real, honest, standing limitation named directly

Even once every phase above is real and tested, running a full DCGAN training job to visual
convergence in THIS sandbox is a real, separate, likely-infeasible undertaking — no GPU, no real
image dataset staged, and CPU-only im2col-based convolution is genuinely slow at real image
resolutions. The real, honest goal of this plan is a working, correctly-differentiable set of
primitives, verified the same rigorous way (`make test-*`, hand-computed reference values) every
other real feature in this stdlib already is — not a claim that a trained, image-generating model
comes out the other end of this sandbox specifically.

## Related

- `stdlib/nn.prn`, `stdlib/linalg.prn`, `stdlib/array.prn`, `stdlib/stats.prn` — the real,
  existing foundation this plan builds on.
- `EMILY/BACKLOG.md` SECTION 210 — the real trigger and this doc's own tracking entry.
- `docs/EXPR_POSITION_BINDING_FORMS_NORTHSTAR.md` — the direct structural precedent this doc
  follows (name real options, make a real, decisive recommendation, leave code for a later pass).
