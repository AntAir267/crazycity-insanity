# Box against box: contacts between two computer vehicles

This page says how MM2 finds the contacts between two **box** bounds that both move, in one
physics sample, and turns them into the impact list that `collision.md` ("Collision response")
resolves. In a race this is every collision between two computer vehicles: opponent, police and
traffic cars all collide with a box (`car-collision.md` 3.1). A pair in which either bound is a
polygon bound (the player's car) uses the polygonal method instead (`car-collision.md` 3.2).

Single precision throughout, operations in the order written (`vehicle.md`, "Precision"); the
grouping of every sum of three terms is listed in "Evaluation orders" at the end. Comparisons are
written as they must be done; where a comparison can meet a NaN, the text says which way a NaN
goes ("NaN: yes" = the condition counts as true for a NaN). In a race the penetration skin is 0
(`collision.md`); section 7 says what it would change.

## Terms

- **Box**: size S and offset c (the box centre in the body's coordinates), both vectors. From
  them: h = S·0.5 per component; the far corner m = c + h per component; and the **half size**
  e = m − c per component, which this test uses everywhere (it equals h unless the rounding of
  c + h loses bits). When both boxes have c = (0, 0, 0), MM2 uses the variant of section 8.
- **Corners** k = 0…7 of a box, with the sign vector σ_k (components ±1):
  0 (+,+,+), 1 (−,+,+), 2 (−,−,+), 3 (+,−,+), 4 (+,+,−), 5 (−,+,−), 6 (−,−,−), 7 (+,−,−).
- **Edges** (first corner, second corner): e0 (0,1), e1 (1,2), e2 (2,3), e3 (3,0), e4 (4,5),
  e5 (5,6), e6 (6,7), e7 (7,4), e8 (0,4), e9 (1,5), e10 (2,6), e11 (3,7).
- **Faces** f = 0…5 with outward unit normals ν_f: 0 (1,0,0), 1 (−1,0,0), 2 (0,1,0), 3 (0,−1,0),
  4 (0,0,1), 5 (0,0,−1). The face's **axis** is f div 2 (x, y or z); f is **even** for the + side.
  Each face has four corners in order (slots 0–3) and four edges, slot k being the edge from corner
  slot k to corner slot k+1 (wrapping):

  | Face | Corners | Edges | View (u, v) |
  |---|---|---|---|
  | 0 | 4, 0, 3, 7 | 8, 3, 11, 7 | (y, z) |
  | 1 | 2, 1, 5, 6 | 1, 9, 5, 10 | (z, y) |
  | 2 | 1, 0, 4, 5 | 0, 8, 4, 9 | (z, x) |
  | 3 | 7, 3, 2, 6 | 11, 2, 10, 6 | (x, z) |
  | 4 | 3, 0, 1, 2 | 3, 0, 1, 2 | (x, y) |
  | 5 | 5, 4, 7, 6 | 4, 7, 6, 5 | (y, x) |

  The **view** of a face is the pair of components (u, v) that 4.2 and 4.3 use as 2D coordinates on
  it (its corners, in slot order, run counter-clockwise in (u, v)).
- **Edges of a corner** (in this order): 0: e0 e3 e8; 1: e0 e9 e1; 2: e2 e1 e10; 3: e3 e2 e11;
  4: e4 e8 e7; 5: e4 e5 e9; 6: e5 e6 e10; 7: e6 e7 e11.
- **Faces of an edge** (first, second): e0 (2,4), e1 (1,4), e2 (3,4), e3 (0,4), e4 (2,5), e5 (1,5),
  e6 (3,5), e7 (0,5), e8 (0,2), e9 (1,2), e10 (1,3), e11 (0,3).
- **Matrices**: rows X, Y, Z (the body axes in world coordinates) and origin o, treated as
  orthonormal and never renormalised. M_A, M_B are the two bodies' current matrices; M_A0, M_B0
  their previous matrices as `car-collision.md` hands them over (each possibly shifted by its last
  push). **Turning** a vector w by M gives w.x·X + w.y·Y + w.z·Z; **placing** a point adds o.
  **Into** M: (X·w, Y·w, Z·w).
- dir = M_B.o − M_A.o per component. dt: the sample's time step (`vehicle.md`); 1/dt is not used.
- **Displacement** d_M(x) of a world point x under a body with matrices M (now) and M₀ (before):
  exactly as `collision-contacts.md` ("Terms", "Displacement of a body point").
- **Sign bit** of a float: its top bit (−0 has it set).

## 1. The method in one paragraph

Each box is described in the other's frame, centred on the other's box centre, with the help of
the relative orientation R of the two bodies, whose nearly-zero entries mark axes that are nearly
perpendicular. A **face pass** then takes one box (V) against the faces of the other (F): for every
face of F on the side where V was in the previous sample, it finds V's corner deepest below that
face; if that corner is above the face plane, the boxes are apart and the test ends with no
contact. Otherwise, depending on how V is turned against that face, the contact is a corner of V
on the face, an edge of V lying along the face, or a face of V lying on the face. Every such
**candidate** (one to eight impacts) is offered to an acceptance rule (5) that keeps the shallower
candidate (and one reached sooner while that is "fast"); an accepted candidate replaces all
earlier impacts. Two face passes run (A against B's faces, then B against A's faces), then an
**edge pass** tries pairs of edges that both face passes pointed at. The impacts of the last
accepted candidate are the result.

## 2. Setup

### 2.1 Relative orientation

R[i][j] = (row i of M_A)·(row j of M_B), i, j = 0, 1, 2 (rows X, Y, Z). Row i of R is A's axis i in
B's coordinates; column j is B's axis j in A's coordinates.

### 2.2 Sign classes of R

Each entry x of R gets a **coarse sign** s and an **exact sign** σ:

| x | s | σ |
|---|---|---|
| x > 0.035 | 1 | 1 |
| 0 < x ≤ 0.035 | 0 | 1 |
| x < −0.035, or NaN | −1 | −1 |
| −0.035 ≤ x < 0 | 0 | −1 |
| x = ±0 | 0 | 0 |

(0.035 is the float 0.035000000149.) Let z be the number of entries with s = 0. Then:

- z = 2: **promote** once; z = 3: promote twice;
- z = 4: unless s is **balanced**, promote three times;
- z = 5: the five-zero rule;
- any other z: nothing.

**Promote**: among the entries with s = 0 and σ ≠ 0, scanning rows 0, 1, 2 and in each row
columns 0, 1, 2, take the first one with the largest |x| (a later entry replaces the current pick
only if |x| > the pick's |x| strictly; the pick starts at 0, so |x| must be > 0); set its s = σ. If
there is none, nothing changes.

**Balanced**: every row and every column of s has exactly one or two zeros.

**Five-zero rule**: scanning as for promote, for each entry with s = 0, σ ≠ 0 and |x| strictly
greater than the best so far (starting at 0): set s = 1 for a moment; if s is now balanced, this
entry becomes the pick and its |x| the best; set s back to 0. Afterwards the pick gets s = σ. If
nothing was picked, MM2 writes outside its table: the exact sign σ of R[2][0] becomes negative (it
then counts as −1 wherever σ is read) and s stays as it was. (It needs five near-zero entries of a
rotation whose other small entries are exact zeros; not seen in play.)

### 2.3 Centres

All in the **centred frame** of a box: its axes, origin at its box centre.

- qA, A's current box centre in B's centred frame: w = (A's offset turned by M_A) − dir, then
  qA = (w into M_B) − c_B per component.
- pA, A's previous box centre in B's previous centred frame: w = (A's offset placed by M_A0) −
  M_B0.o, then pA = (w into M_B0) − c_B.
- qB, B's current box centre in A's centred frame: w = (B's offset turned by M_B) + dir, then
  qB = (w into M_A) − c_A.
- pB, B's previous box centre in A's previous centred frame: w = (B's offset placed by M_B0) −
  M_A0.o, then pB = (w into M_A0) − c_A.

The previous centres are taken just before the face pass that needs them, qB just before pass 2;
the order does not change any value.

### 2.4 Working state of one test

- The **edge-pair table** T[a][b], a = an edge of A, b = an edge of B, 144 small sets of the marks
  1, 2 and 4, all empty at the start (4.4 sets them, 6 reads them).
- The impact count N = 0; the **best time** and **best depth** of the acceptance rule (5), read only
  while N > 0; fast = dt·1.5.
- Eight **impact slots** 0…7. A slot holds kind, two feature numbers, position P, normal n,
  depth, and its **sides**: which box is the impact's first side ("A" of `collision.md`'s impact
  record) and which the second. The result is slots 0 … N−1 (7).

## 3. Corners of one box in the other's centred frame

For a box V with half size e whose centre is at t in F's centred frame, and whose axes in F's
coordinates are the rows a₀, a₁, a₂ (pass 1: the rows of R; pass 2: the columns of R, i.e.
a_k = (R[0][k], R[1][k], R[2][k])):

1. α = a₀·e.x, β = a₁·e.y, γ = a₂·e.z (each component times the scalar).
2. u = β + α, w = β − α (per component).
3. Before the shift: K₀ = u + γ, K₁ = w + γ, K₂ = γ − u, K₃ = γ − w, K₄ = −K₂, K₅ = −K₃,
   K₆ = −K₀, K₇ = −K₁ (per component; K₄…K₇ are exact negations).
4. Each K_k = K_k + t per component.

K_k is V's corner k (sign vector σ_k). The rows a₀, a₁, a₂ are kept for the face pass (the
**V axes**).

## 4. The face pass: box V against the faces of box F

| | Pass 1 | Pass 2 |
|---|---|---|
| V, F | A, B | B, A |
| corners (3) | half size e_A, centre qA, rows of R | e_B, qB, columns of R |
| previous centre p_V | pA | pB |
| F's half size e_F, offset c_F | e_B, c_B | e_A, c_A |
| matrices (V now, V before, F now, F before) | M_A, M_A0, M_B, M_B0 | M_B, M_B0, M_A, M_A0 |
| sides of its impacts (first, second) | A, B | B, A |
| face against face (4.2) | yes | only its marks (step 6) |
| edge-pair mark (4.4) | 1, entry T[V edge][F edge] | 2, entry T[F edge][V edge] |
| sign of entry (i, k) below | s and σ of R[i][k] | s and σ of R[k][i] |

For each face f = 0, 1, …, 5 of F, with n = ν_f and its axis k:

1. If not (n·p_V > 0) (NaN: skip), skip the face. (Only faces on the side where V's centre was in
   the previous sample.)
2. g = −1 if f is even, else +1. For V's axes i = 0, 1, 2: s_i = g·(coarse sign of entry (i, k)),
   and the bit b_i = (σ of entry (i, k) ≥ 0), inverted if f is even.
3. The **deepest corner** of V: the corner whose sign vector has + on axis i exactly when b_i is
   set: index c = table[b₀ + 2·b₁ + 4·b₂] with table = 6, 7, 5, 4, 2, 3, 1, 0. v = K_c.
4. hk = |n·e_F|; depth d = hk − n·v.
5. If d < 0 (NaN: yes): **the boxes are apart**. The whole test ends now with no impacts (N = −1;
   earlier impacts are dropped).
6. By the coarse signs:
   - all three of s₀, s₁, s₂ non-zero: **corner against face**, 4.1;
   - exactly one of them zero: **edge against face**, 4.3;
   - two or three of them zero: mark corner c on face f (4.4); then in pass 1 **face against
     face**, 4.2; in pass 2 nothing more.

s_i = +1 means V's axis i points into F (against n); the deepest corner lies on V's + side of
that axis.

### 4.1 Corner against face

1. **Inside** test: with (p, q) the two components other than the face axis k (k = x: y and z;
   k = y: x and z; k = z: x and y): |v.p| ≤ e_F.p and |v.q| ≤ e_F.q (each NaN: yes).
2. If inside:
   1. n_w = n turned by M_F (F's face normal in world coordinates).
   2. r = d·0.5; P_l = (n·r + v) + c_F per component (n.x·r + v.x, then + c_F.x); P = P_l placed by
      M_F (the middle of the overlap, in world coordinates).
   3. Offer the candidate (5) with P, n_w, d. If accepted: slot 0 = kind 1, features (c, f),
      position P, normal n_w, depth d, sides of the pass; N = 1.
   4. If not accepted: hk′ = |n_w·e_F|; for every corner u ≠ c of V (u = 0…7): if n_w·K_u < hk′
      (NaN: yes), mark corner u on face f (4.4). (MM2 uses the world normal here; see "Things that
      look like mistakes".)
3. In every case (inside or not), mark corner c on face f.

### 4.2 Face against face (pass 1 only)

1. F's corners in its centred frame: L_k = σ_k·e_F per component for k = 0…3; L₄ = −L₂, L₅ = −L₃,
   L₆ = −L₀, L₇ = −L₁.
2. **V's face**: the first of these that applies: s₀ = 1 → face 0, normal N_V = a₀; s₀ = −1 → face
   1, N_V = −a₀; s₁ = 1 → face 2, a₁; s₁ = −1 → face 3, −a₁; s₂ = 1 → face 4, a₂; s₂ = −1 → face 5,
   −a₂. (N_V is V's outward face normal in F's frame; this face looks at F.) Call it φ.
3. Two quads in the view (u, v) of face f (Terms): **P** = the views of V's corners
   K at face φ's corner slots 0, 1, 2, 3; **Q** = the views of F's corners L at face f's corner
   slots 3, 2, 1, 0 (reversed). For each slot j = 0…3 of φ, give the entries T[φ's edge slot j][each
   edge of face f] the mark 4.
4. Run the overlap walk (4.2.1) on P and Q. If it gives no records, this face gives nothing; go to
   the next face.
5. Turn the records into points (4.2.2). If no point is kept, **the boxes are apart**: the test
   ends with no impacts (N = −1).
6. With the kept points 0 … q−1 (depths d_i, points X_i):
   D = (…((d₀ + d₁) + d₂) …) + d_(q−1), and per component S = (…(X₀ + X₁) …) + X_(q−1)
   (left to right; q = 1 means D = d₀, S = X₀). w = 1.0 / q.
7. n_w = n turned by M_F. Average point: Y_l = S·w + c_F per component (S.x·w, then + c_F.x);
   Y = Y_l placed by M_F.
8. Offer the candidate (5) with Y, n_w and depth w·D. If not accepted, go to the next face.
9. If accepted: N_w = N_V turned by M_F. For each kept point i: slot i = kind (the record's type,
   4.2.2), its two features, depth d_i, position = X_i placed by M_F (**without** c_F; see
   "Things that look like mistakes"), normal = −N_w for a record of type 2, else n_w; sides of the
   pass. N = q.

#### 4.2.1 The overlap walk

The walk finds the outline of the overlap of two convex quads (MM2 uses it with 4 + 4 corners) as
a list of **records**: a **crossing** (edge i of P crosses edge j of Q, with the parameters t_P
along P's edge and t_Q along Q's edge), a **P corner** i (inside Q) or a **Q corner** j (inside P).
Records are kept in the order made; MM2 has room for 8, and two quads never gave more. The walk
handles collinear edges its own way: for example, two identical quads, or two quads whose first
edges (P's edge 0 and Q's edge 0) lie on one line, give no records at all.

For a polygon W with corners W₀ … W₃, edge k runs from W_k to W_(k+1) (W₄ = W₀): its vector
E_k = (W_(k+1).u − W_k.u, W_(k+1).v − W_k.v), its constant C_k = E_k.u·W_k.v − E_k.v·W_k.u, and the
**side value** of a point X against it: side_W,k(X) = (X.v·E_k.u − E_k.v·X.u) − C_k.

State: an index i on P and j on Q (both 0 at the start); the **walker** (Q at the start; the other
polygon is "the other"); four side values: hw = the walker's next corner (after its index) against
the other's current edge, tw = the walker's previous hw (its current corner), ho = the other's next
corner against the walker's current edge, to = the other's current corner against the walker's
current edge. In formulas, with walker Q: hw = side_P,i(Q_(j+1)), ho = side_Q,j(P_(i+1)),
to = side_Q,j(P_i); with walker P: hw = side_Q,j(P_(i+1)), ho = side_P,i(Q_(j+1)), to =
side_P,i(Q_j) (indices wrap at 4). "Recompute hw / ho / to" means evaluate that formula with the
current walker, i and j. Also: two countdowns a (for P) and b (for Q), both 4; a budget of 16.

Operations:
- **Advance**: the walker's index += 1, wrapping 4 to 0, and the walker's countdown −= 1. Then, if
  there is at least one record and (a < 0, or b < 0, or both a ≤ 0 and b ≤ 0), the walk **stops**.
- **Swap**: the other becomes the walker; hw = ho and tw = to (old values).
- **Crossing record**: if there are records and the first one is a crossing with the same i and j,
  the walk stops instead. Otherwise, if it is the first record, a = 4 and b = 4. The record gets
  i, j and the walker's edge parameter tw/(tw − hw) and the other's edge parameter to/(to − ho)
  (each a subtraction then a division), as t_P and t_Q according to which polygon walks.
- **Corner record** of the walker's or the other's current corner (P corner i or Q corner j): if
  there are no records, a = 4 and b = 4; if there are records and the first one is a corner record
  of the same polygon with the same index, the walk stops instead; otherwise append it.
- **All corners** of a polygon: corner records of its corners 0, 1, 2, 3 in order (these are the
  first records, so none of them stops the walk).

Start: i = j = 0, walker Q, hw = side_P,0(Q₁), tw = side_P,0(Q₀), ho = side_Q,0(P₁),
to = side_Q,0(P₀). Then:

- If hw ≥ 0 (NaN: no):
  - if ho < 0 (NaN: yes): if to ≤ 0 (NaN: yes) or tw ≥ 0 (NaN: no), phase S1; else if hw = 0
    (NaN: yes), phase T2; else a crossing record and phase T1;
  - else: if hw < tw (NaN: yes), phase S1; else if ho < to (NaN: yes), swap and phase S1; else the
    walk stops with no records.
- Else (hw < 0 or NaN):
  - if ho < 0 (NaN: yes): phase S2, and if to < ho (NaN: yes), swap;
  - else: if to < 0 (NaN: yes) and tw > 0 (NaN: no), then phase T2 if ho = 0 (NaN: yes), else a
    crossing record and phase T1; otherwise phase S1. In all three cases, swap afterwards.

**Phase S1** (searching). low = (hw < tw) (NaN: yes). Repeat:
1. budget −= 1; if budget ≤ 0: all corners of the other polygon, and the walk stops.
2. Advance; tw = hw; recompute hw.
3. If hw < 0 (NaN: yes): recompute to, then ho.
   - If to < 0 (NaN: yes): if ho ≤ 0 (NaN: yes), next phase T2 if ho = 0 (NaN: yes), else S2;
     otherwise next phase T1 after a record: if tw ≤ 0 (NaN: yes) a corner record of the walker's
     current corner, else a crossing record. Then swap, and go to the next phase.
   - Else: low = false; swap; repeat.
4. Else (hw ≥ 0): if low is set: the walk stops with no records if hw ≥ tw (NaN: no), else
   repeat. If low is clear: low = (hw < tw) (NaN: yes); repeat.

**Phase S2** (searching, the walker behind). Repeat:
1. budget −= 1; if budget ≤ 0: all corners of the walker's polygon, and the walk stops.
2. Advance; tw = hw; recompute ho.
3. If ho ≥ 0 (NaN: no): recompute to; swap; phase S1.
4. Recompute hw. If hw < 0 (NaN: yes), repeat.
5. Recompute to. If to > 0 (NaN: no): phase T2 if hw = 0 (NaN: yes), else a crossing record and
   phase T1.
6. Else if to ≠ 0 (a NaN counts as 0): phase S1 (no swap).
7. Else if hw > 0 (NaN: no): a corner record of the other's current corner; phase T1.
8. Else repeat.

**Phase T1** (tracing). Repeat:
1. Advance (it may stop the walk).
2. tw = hw; recompute hw. If hw ≥ 0 (NaN: no), repeat.
3. Recompute ho. If ho > 0 (NaN: no): if tw > 0 (NaN: no), recompute to and make a crossing
   record; else a corner record of the walker's current corner (to is not recomputed). Otherwise
   (ho ≤ 0 or NaN) the next phase is T2.
4. Swap; continue in the current phase (T1, or T2 if step 3 said so). A record that stopped the
   walk ends it.

**Phase T2** (tracing along a shared line). Repeat:
1. Advance (it may stop the walk).
2. tw = hw; recompute ho; a corner record of the walker's current corner (it may stop the walk).
3. If ho ≥ 0 (NaN: no): swap (to is not recomputed); phase T1.
4. Recompute hw. If hw > 0 (NaN: no): phase T1, after a crossing record (with to recomputed first)
   if tw < 0 (NaN: yes).
5. Else repeat.

Phases S1 and S2 never have records (every path that makes the first record goes on to T1 or T2),
so the stop of step 4 of S1 and the stop at the start always end with no records.

#### 4.2.2 Records to points

For each record in order (a, b, the corners at face φ's slots i and i+1 of V; F_c = F's corner L at
face f's slot 0; V_c = V's corner K at face φ's slot 0; "depth below a plane" uses the dot product
(w.z·m.z + w.y·m.y) + w.x·m.x):

- **Crossing** (i, j, t_P): X = (K_b − K_a)·t_P + K_a per component, the corners being φ's slots i
  and i+1 (wrapping); depth = (F_c − X)·n. Features: φ's edge slot i, and face f's edge slot
  (2 − j) mod 4 (the reversed quad's edge j).
- **P corner** i: X = K at φ's slot i; depth = (F_c − X)·n. Features: that corner, face f.
- **Q corner** j: X = L at face f's slot 3 − j; depth = (V_c − X)·N_V. Features: that corner of F,
  face φ.

If depth < 0 (NaN: yes) the record is dropped. Otherwise the kept point is X + D·(depth·0.5) per
component (D.x·r + X.x with r = depth·0.5), with D = n for crossings and P corners and D = N_V for
Q corners; its type is 0 (crossing), 1 (P corner) or 2 (Q corner).

### 4.3 Edge against face

V has an edge parallel to face f (its axis with s = 0), running from the deepest corner c.

1. **The edge**: by (s₀, s₁, s₂) (exactly one is 0):

   | s₀ | s₁ | s₂ | edge |
   |---|---|---|---|
   | 1 | 1 | 0 | e8 |
   | 1 | 0 | 1 | e3 |
   | 1 | 0 | −1 | e7 |
   | 1 | −1 | 0 | e11 |
   | 0 | 1 | 1 | e0 |
   | 0 | 1 | −1 | e4 |
   | 0 | −1 | 1 | e2 |
   | 0 | −1 | −1 | e6 |
   | −1 | 1 | 0 | e9 |
   | −1 | 0 | 1 | e1 |
   | −1 | 0 | −1 | e5 |
   | −1 | −1 | 0 | e10 |

   Call it ε. Mark edge ε on face f (4.4). The far corner o = ε's first corner, or its second if
   the first is c.
2. E = K_o − K_c per component. **Clip** (4.3.1) the line K_c + t·E to face f's rectangle →
   ok, t₀, t₁, slots k₀, k₁.
3. If not ok: mark corners c and o on face f; next face.
4. If t₀ < 0 (NaN: yes), t₀ = 0. If t₁ > 1 (NaN: no), t₁ = 1.
5. X₀ = E·t₀ + K_c, X₁ = E·t₁ + K_c (per component, E.x·t then + K_c.x).
   hk = |n·e_F|; d₁ = hk − n·X₁; d₀ = hk − n·X₀.
6. **Both ends below the face** (d₀ ≥ 0 and d₁ ≥ 0, NaN: no):
   1. n_w = n turned by M_F. P₀ = ((n·(d₀·0.5) + X₀) + c_F) placed by M_F, as in 4.1.
      P₁ = ((X₁ + c_F) placed by M_F) + n_w·(d₁·0.5) per component (n_w.x·r₁ + W.x, r₁ = d₁·0.5:
      the half-depth step is taken in world coordinates here).
   2. Middle Y = (P₁ + P₀)·0.5 per component; offer the candidate (5) with Y, n_w and depth
      (d₁ + d₀)·0.5. If not accepted: next face (no marks).
   3. Slot 0: position P₀, normal n_w, depth d₀; slot 1: P₁, n_w, d₁. Slot 0's kind: if t₀ = 0
      (NaN: yes), kind 1 with features (c, f), and mark corner c on face f; else kind 0 with
      features (ε, face f's edge slot k₀).
   4. If t₁ ≤ t₀ (NaN: yes): N = 1. Slot 0's sides are **not** written in this case (MM2 leaves
      what the slot held; see "Things that look like mistakes"); slot 1 is filled but not counted.
   5. Otherwise slot 1's kind: if t₁ = 1 (NaN: yes), kind 1 with (o, f), and mark corner o on face
      f; else kind 0 with (ε, face f's edge slot k₁). Both slots get the sides of the pass; N = 2.
7. **Only the near end below** (d₀ ≥ 0, d₁ < 0 or NaN): n_w and P₀ as in 6.1. Kind and features as
   slot 0's in 6.3, including the mark of corner c when t₀ = 0, made **before** the offer. Offer
   (5) with P₀, n_w, d₀; if accepted: slot 0 = (kind, features, P₀, n_w, d₀, sides); N = 1.
8. **Only the far end below** (d₀ < 0 or NaN): if d₁ < 0 (NaN: yes), **the boxes are apart**: the
   test ends with no impacts (N = −1). Else n_w as above; P₁ = ((n·(d₁·0.5) + X₁) + c_F) placed by
   M_F (as in 4.1). Kind: if t₁ = 1 (NaN: yes), kind 1 with (o, f), marking corner o on face f
   before the offer; else kind 0 with (ε, face f's edge slot k₁). Offer with P₁, n_w, d₁; if
   accepted: slot 0 = (kind, features, P₁, n_w, d₁, sides); N = 1.

#### 4.3.1 Clipping a line to a face rectangle

Line X + t·E, face with normal n (one of ν_f) and the box's half size e. Take the first of: n.x > 0
(NaN: no) → (u, v) = (y, z); n.x < 0 (NaN: yes) → (z, y); n.y > 0 → (z, x); n.y < 0 → (x, z);
n.z > 0 → (x, y); n.z < 0 → (y, x) — the face's view. With x_u, x_v the components of X, d_u, d_v of
E and h_u, h_v of e:

1. Crossings: none yet.
2. If d_u ≠ 0 (a NaN counts as 0): i = 1.0/d_u.
   - t = (h_u − x_u)·i; if |d_v·t + x_v| ≤ h_v (NaN: yes), add the crossing (t, slot 0).
   - t = ((−h_u) − x_u)·i; if |d_v·t + x_v| ≤ h_v (NaN: yes), add (t, slot 2).
3. If d_v ≠ 0 (NaN counts as 0) and fewer than two crossings: i = 1.0/d_v.
   - t = (h_v − x_v)·i; if |t·d_u + x_u| ≤ h_u (NaN: yes), add (t, slot 1).
   - If still fewer than two crossings: t = ((−h_v) − x_v)·i; if |t·d_u + x_u| ≤ h_u, add (t,
     slot 3).
4. **Adding** (t, k): the first crossing sets t₀ = t₁ = t and k₀ = k₁ = k. Any later one: if t < t₀
   (NaN: yes), t₀ = t and k₀ = k; else if t > t₁ (NaN: no), t₁ = t and k₁ = k. (The count becomes
   two even if neither changes.)
5. No crossing: ok = no (t₀ = t₁ = 0.5). Otherwise ok = no if t₀ ≤ 0 and t₁ ≤ 0 (each NaN: yes),
   else ok = yes if t₀ < 1 or t₁ < 1 (each NaN: yes), else no.

The slots name the face's edges: slot 0 is the side u = +h_u, 1 is v = +h_v, 2 is u = −h_u, 3 is
v = −h_v, which are the face's edge slots 0–3 of the table in "Terms".

### 4.4 Edge-pair marks

The marks collect, for the edge pass (6), edge pairs that both face passes found near each other.
"Mark" means: add mark 1 to T[V edge][F edge] in pass 1, or mark 2 to T[F edge][V edge] in pass 2
(the table is always indexed [edge of A][edge of B]); 4 means add mark 4 to the same entry.

- **Mark corner u on face f**: for each of the three edges of corner u (V's edges) and each of the
  four edges of face f (F's edges): mark.
- **Mark edge ε on face f**: for each edge of face f: add 4 (to T[ε][that edge] in pass 1,
  T[that edge][ε] in pass 2).
- In 4.2 step 3 (pass 1 only): add 4 to T[φ's edge][each edge of face f] for all four edges of φ.

## 5. Acceptance of a candidate

A candidate (point P in world coordinates, normal n in world coordinates pointing out of F into V,
depth d) is offered with the matrices of V and F of the pass (in the edge pass: V = A, F = B):

1. δ_V = d_V(P) and δ_F = d_F(P), the displacements of P under each body (Terms).
2. ρ = (δ_F − δ_V)·n (each component difference first): how far F moved towards V along n in this
   sample, at P.
3. d′ = d − skin (= d in a race).
4. If ρ > 0 (NaN: no): if d′ > 0 (NaN: no), τ = d′/ρ; else τ = 0 and d′ = 0. Otherwise τ = the
   largest float (3.4028235e38).
5. If N > 0, reject the candidate when (best time < fast (NaN: yes) and best time ≤ τ (NaN: yes)),
   or when best depth ≤ d′ (NaN: yes).
6. Otherwise accept it: best time = τ, best depth = d′.

So the shallowest candidate wins, except that while the current best is "fast" (τ < 1.5·dt) only
a candidate with a smaller τ can replace it. τ is a fraction of this sample's motion, compared with
a time (see "Things that look like mistakes"). The first candidate offered with N = 0 is always
accepted, and an accepted candidate always rewrites the slots and N.

## 6. The edge pass

After both face passes (if neither ended the test). Here the corners K are B's corners in A's
centred frame (pass 2's corners), and the **B normals** are B's face normals in A's frame:
N_B,0 = column 0 of R, N_B,1 = −(column 0), N_B,2 = column 1, N_B,3 = −(column 1), N_B,4 =
column 2, N_B,5 = −(column 2) (each column as a vector (R[0][k], R[1][k], R[2][k])). A's face
normals in its own frame are ν_f.

For each edge i of A (0…11, outer loop) and each edge j of B (0…11, inner loop) with T[i][j]
holding exactly the marks 1 and 2 (no 4):

1. A's edge i = (a, b): Δ_A = (σ_b − σ_a)·e_A per component (exactly 0 or ±2·e_A). B's edge j
   faces (f₀, f₁). If the sign bits of Δ_A·N_B,f₁ and Δ_A·N_B,f₀ are equal, skip the pair.
2. B's edge j = (a′, b′): Δ_B = K_b′ − K_a′. A's edge i faces (g₀, g₁). If the sign bits of
   Δ_B·ν_g₁ and Δ_B·ν_g₀ are equal, skip.
3. s_A = ν_g₁ + ν_g₀, s_B = N_B,f₁ + N_B,f₀ (per component). Unless s_A·s_B < 0 (NaN: yes), skip.
   (The two edges' outward directions are opposed.)
4. Closest points of the two edge lines: `collision-contacts.md`, 3d, edge-edge test step 5, with
   A's edge as the polygon edge (A = σ_a·e_A, B = σ_b·e_A per component, pe = Δ_A) and B's edge as
   the car edge (S = K_a′, E = K_b′, ce = Δ_B). Skip the pair if not ok. This gives nrm (pointing
   from B's edge line towards A's edge line), depth, and the point Π on B's edge line.
5. ε₀ = nrm·N_B,f₀, ε₁ = nrm·N_B,f₁.
   - If ε₀ > 0 and ε₁ > 0 (each NaN: no): **the boxes are apart**: the test ends with no impacts,
     dropping any found so far.
   - If exactly one of them is > 0, skip the pair.
6. Unless nrm·qB > 0 (NaN: no), skip the pair. (B's centre must lie ahead along nrm: the edges
   have passed each other.)
7. r = depth·0.5; P_l = (nrm·r + Π) + c_B per component; P = (P_l placed by M_A) − c_A per
   component (**c_B and c_A**: see "Things that look like mistakes"). n_w = (−nrm) turned by M_A
   (out of B into A).
8. Offer (5) with P, n_w, depth and V = A, F = B. If accepted: slot 0 = kind 0, features (i, j),
   position P, normal n_w, depth, sides (A, B); N = 1.

## 7. The result

If the test ended with "apart" (N = −1), or N = 0 after the edge pass, there are no impacts.
Otherwise the impacts are slots 0 … N−1, in slot order, all from one candidate (the last one
accepted):

| Candidate | Impacts | Sides (first, second) |
|---|---|---|
| corner against face (4.1) | 1 | the pass's |
| face against face (4.2), pass 1 only | 1–8, in walk order | (A, B) |
| edge against face (4.3) | 1 or 2 (near end first) | the pass's |
| edge against edge (6) | 1 | (A, B) |

- **Normal**: every stored normal points out of the impact's **second** side into its **first**
  side. Impacts from pass 1 and from the edge pass have first = A, second = B: their normal points
  out of B into A, exactly the convention of `collision.md` ("The impact record") with A = box A.
  Impacts from pass 2 have first = B, second = A: their normal points out of A into B. To state
  every normal out of B into A, negate those; but for a bit-exact response resolve a pass-2 impact
  with the record's own sides (`collision.md`'s A = box B, B = box A, normal as stored), since the
  response is not exactly symmetric in its two sides.
- **Depth** ≥ 0, the impact's own (for face-face points, each point's own depth, not the average).
- **Position** world coordinates (with the offsets of the mistakes section).
- The **weight** of each impact is 1/N (`collision.md`), and the impacts are resolved in slot order.
- **Materials**: each side's box material (a box has one material; `car-collision.md` 3.4).
- **Kind and features** (0 edge, 1 a corner of the face pass's V or of the clipped edge's end, 2
  a corner of F; the corner, edge or face numbers) are recorded for completeness; the response does
  not use them.
- At most 8 impacts. MM2 runs this test only if its impact buffer has at least 8 free places
  (the caller gives 200), so the limit is never reached.
- **Skin**: the test uses the smaller of the two bounds' skins; in a race both are 0, so depths are
  the full depths. With a non-zero skin, the acceptance rule (5) compares d − skin and every
  impact also records the skin (the response then pushes only the excess).

## 8. When both boxes have offset (0, 0, 0)

When every component of both offsets compares equal to 0 (a NaN component counts as 0), MM2 uses a
separate copy of this test. It is the same test with c_A = c_B = (0, 0, 0): the same steps,
comparisons, constants, tables, marks, slots and acceptance (and with the offsets zero, the
face-face positions of 4.2 and the edge-edge position of 6 come out where they belong). Its half
sizes are the far corners m (= h). Only the grouping of some sums differs; they are listed under
"Evaluation orders", "Zero-offset variant". (Shipped traffic data always gives a non-zero `CG`, and
an opponent's box offset is the centre of its `.bnd` box, so the variant needs a mod.)

## Things that look like mistakes (keep them)

- **Face-face positions without the offset**: the impacts of 4.2 are placed by F's matrix from the
  centred frame without adding c_F, so they lie −c_F (in F's axes) away from the real contact; for
  a car's box that is typically 0.8 m too low and some centimetres along the car. The average point
  used for the acceptance does include c_F.
- **Edge-edge position**: P = M_A(local + c_B) − c_A, with c_B added in A's axes and c_A subtracted
  in world axes; correct would be M_A(local + c_A).
- **Wrong normal in a corner test**: after a rejected corner-against-face candidate, the "other
  corners below the face" test of 4.1 uses the face normal in world coordinates against corners in
  F's frame. It only changes which edge pairs get marks.
- **Time units**: τ (5) is the depth divided by the closing displacement of this sample, a
  fraction of the sample, but it is compared with fast = 1.5·dt, a time.
- **Stale sides**: edge-face candidates with both ends below the face and t₁ ≤ t₀ (the clipped edge
  degenerates to a point; it needs an edge through a rectangle corner) leave slot 0's sides as the
  slot held them: from an earlier candidate of this test if it wrote slot 0, otherwise from an
  earlier test in the game (MM2's impact buffer is shared). An implementation cannot reproduce the
  latter; use the pass's sides then. The second slot is computed but not counted.
- **Five-zero rule**: when nothing qualifies, MM2 writes outside its table (2.2).
- Separation is decided from single tests: a face's deepest corner above it, a face-face overlap
  without a point below the face, an edge-face edge entirely above the face, or an edge pair whose
  normal points out of both faces of B's edge each end the whole test, even if another feature
  overlaps deeply; and only faces on the side of V's previous centre are looked at.
- Face against face runs only in pass 1 (B's faces); in pass 2 a face of B lying flat on a face of
  A gives only the corner marks.
- The deepest corner for an exactly zero R entry (σ = 0) is chosen as if the entry were positive.
- Pass-2 impacts are filed with B as the first side.

## Evaluation orders

Products commute, so only the grouping of sums matters. "abc" lists the order of a three-term sum
((term a + term b) + term c), terms named by the component index they carry. For "turned by M"
the terms of output component c are M.row_k.c·w.k; for "into M" output component i is
row_i·w; for a dot product a·b the terms are a.k·b.k. Three orders separated by "/" are for the
output components x/y/z.

### Offset case (sections 2–7)

- R[i][j] = row_i(M_A)·row_j(M_B): R[0][0] yxz; R[1][0] and R[2][0] yzx; all others zyx.
- 2.3: A's offset turned by M_A: yzx/xyz/zyx, then − dir. A's offset placed by M_A0: zyx/zyx/zyx,
  then + M_A0.o, then − M_B0.o. B's offset turned by M_B: zyx/zyx/zyx, then + dir. B's offset
  placed by M_B0: xzy/xzy/xzy, + M_B0.o, then − M_A0.o. Every "into": zyx for all three rows; then
  − the offset per component.
- Section 3: as written (two-term sums).
- Face pass: n·p_V xyz; n·e_F zyx; n·v zyx; n turned by M_F (n_w): zyx/zyx/yzx; points placed by
  M_F (4.1, 4.2 step 7, 4.3 P₀, P₁ and the W of 4.3 step 6.1): xzy/zyx/zyx, + origin last; the
  marks after a rejected corner (4.1 step 2.4): n_w·e_F zyx, n_w·K_u xzy.
- 4.2.2: depth sums zyx. 4.2 step 9: points placed by M_F zyx/xzy/xzy (+ origin); N_V turned by
  M_F xzy/zyx/zyx.
- 4.3 step 5: n·X₁ and n·X₀ zyx (as (n.z·X.z + n.y·X.y) + X.x·n.x).
- 4.2.1: each side value as written: (X.v·E.u − E.v·X.u) − C, the two products first.
- 5: ρ zyx; the displacements as `collision-contacts.md`.
- 6: Δ_A·N_B,f₁ zxy; Δ_A·N_B,f₀ yxz; Δ_B·ν_g₁ yzx; Δ_B·ν_g₀ xzy; s_A·s_B xzy; ε₀ zyx; ε₁ xzy;
  nrm·qB xzy; P_l placed by M_A yxz/zyx/xzy (+ origin, then − c_A); (−nrm) turned by M_A
  xzy/xzy/xzy. Step 4's orders are those of `collision-contacts.md` 3d step 5.

### Zero-offset variant (section 8)

Everything as in the offset case except:

- 2.3: qA = (−dir) into M_B with row X yxz and rows Y, Z zyx; pA = (M_A0.o − M_B0.o) into M_B0,
  qB = dir into M_A, pB = (M_B0.o − M_A0.o) into M_A0, all zyx. No offset terms; half sizes m.
- Face pass: n·v xzy; n turned by M_F: zyx/xzy/xyz; points placed by M_F (4.1, 4.2 step 7, 4.3
  P₀, P₁ and W): zyx/xzy/xzy (+ origin last); 4.1 step 2.4: n_w·K_u zxy (n_w·e_F unchanged, zyx);
  4.2 step 9: points placed by M_F zyx/zyx/zyx, N_V turned by M_F xzy/xzy/xzy.
- 6: Δ_A·N_B,f₁ zyx; Δ_A·N_B,f₀ xzy; Δ_B·ν_g₁ zxy; Δ_B·ν_g₀ zyx; ε₁ zyx; P_l placed by M_A
  xzy/zyx/xzy (no − c_A); (−nrm) turned by M_A xzy/zyx/xzy.
- The additions of c_F (4.1, 4.2 step 7, 4.3) and of c_B and c_A (6 step 7) are absent (with zero
  offsets they would only turn a −0 into +0).
