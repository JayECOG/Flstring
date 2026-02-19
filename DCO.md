# Developer Certificate of Origin — fl (Forever Lightweight)

Version 1.0 — Effective from the first public release of `fl`.

---

## 1. Purpose and Scope

This Developer Certificate of Origin ("DCO") governs all Contributions submitted to the
**fl (Forever Lightweight) C++ string library** (the "Project"), maintained by Jayden Emmanuel
("Copyright Holder"). It establishes a lightweight, legally meaningful mechanism by which every
Contributor certifies the provenance and licensing of their Contribution before it is accepted
into the Work.

The DCO does not transfer copyright ownership. It does not replace or supersede the Contributor
Licence Agreement (CLA), where one has been executed. Rather, the DCO operates in addition to
the FL Licence (see `LICENSE.txt`), and together these instruments ensure that every line of code,
documentation, test, benchmark, comment, and configuration in the Project is contributed with
clear legal authority.

Every person who submits a Contribution to the Project **must** certify this document by including
a `Signed-off-by` trailer in every commit they contribute, as described in Section 5.

---

## 2. Background and Rationale

Open-source projects face genuine legal risk when code is contributed without a clear statement of
provenance, licensing authority, or ownership. Disputes over intellectual property can arise when:

- A contributor incorporates third-party code without appropriate permission;
- An employer or contracting party holds rights over code an employee or contractor submits;
- Prior art or existing patents are knowingly or unknowingly incorporated;
- A contributor misrepresents the licence under which upstream code was made available.

This DCO addresses each of those risks by requiring every contributor to make a positive, on-record
certification — embedded in the commit history — that they have the legal right to submit the
Contribution and that the Contribution is properly licensed for inclusion in the Project. This
certification is made by the contributor personally and is recorded permanently in the version
control history of the Project.

---

## 3. Definitions

For purposes of this DCO:

**"Contribution"** means any original work of authorship (including but not limited to source code,
header files, build scripts, test code, benchmarks, documentation, comments, configuration files,
data files, patches, bug fixes, performance improvements, and any other materials) that a
Contributor intentionally submits to the Copyright Holder for incorporation into, or use with, the
Work. A Contribution does not include work submitted incidentally (for example, as part of
automated telemetry) unless explicitly accepted by the Copyright Holder.

**"Contributor"** means any individual natural person who submits a Contribution. Where a
Contribution is made by an individual acting on behalf of an employer or other organisation, both
the individual and (where applicable) the organisation are considered Contributors with respect to
that Contribution.

**"Covered Licence"** means any one of the following open-source licences (listed by SPDX
identifier), provided that the licence permits the Copyright Holder to relicence the Work under
the FL Licence or a future version thereof: MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, BSL-1.0,
ISC, Unlicense, CC0-1.0. A Contribution sourced from work under any other licence requires the
prior written approval of the Copyright Holder.

**"Work"** means the fl library and all associated source code, documentation, tests, benchmarks,
build infrastructure, and other materials that together constitute the Project, as maintained in
the Project's canonical repository.

**"Sign-off"** means a textual certification made by the Contributor in the commit message, as
described in Section 5.

**"You"** means the Contributor certifying this DCO by providing a Sign-off.

---

## 4. The Developer Certificate of Origin

By making a Sign-off on a commit, You certify that:

**(a) Own Creation.**
The Contribution was created in whole by You, and You have the right to submit it under the
FL Licence (see `LICENSE.txt`). No part of the Contribution was created by, or belongs to, a
third party whose consent has not been obtained.

**(b) Based on Prior Open-Source Work.**
The Contribution is based upon, or incorporates materials from, prior work that is made available
under a Covered Licence, and You have the right to submit that prior work together with your
modifications under the FL Licence, consistent with the terms of that Covered Licence. You have
identified the prior work and its licence in the Contribution, in a commit message body, or in the
relevant source file's attribution header.

**(c) Third-Party Attestation.**
The Contribution was provided to You, in whole or in part, by a third party who has certified to
You, in writing, that the material was created by them or is otherwise available under a Covered
Licence, and that they have the authority to permit You to submit it under the FL Licence. You
have no reason to believe that certification was inaccurate, misleading, or given without
authority.

**(d) Mixed or Composite Work.**
The Contribution is a combination of works falling under (a), (b), and/or (c) above. Each
distinct component is covered by at least one of those sub-certifications, and You are able to
identify which sub-certification applies to each component.

**(e) Employment and Contractor Relationships.**
If You made the Contribution in the course of Your employment by, or as a contractor for, another
person or organisation, You have confirmed that:

   (i) Your employer or contracting organisation has granted You authority, either expressly or
       by a documented policy that applies to contributions to open-source projects, to submit the
       Contribution under the FL Licence; or

   (ii) Your employer or contracting organisation has signed the `fl` CLA (see `CLA.md`) or has
        otherwise provided written consent to the Copyright Holder permitting the Contribution; or

   (iii) The Contribution falls entirely outside the scope of Your employment or contractual
         duties, was developed entirely on Your own time using Your own resources, and You retain
         full ownership of it under the terms of any applicable employment or contractor agreement.

   You understand that it is Your responsibility to determine which of (i), (ii), or (iii) applies
   and to ensure that the relevant authority or consent has been obtained before signing off.

**(f) Patent Awareness.**
To the best of Your knowledge, the Contribution does not infringe any third-party patent. Where
You are aware of any patent claims that could plausibly be read to cover the Contribution, You
have disclosed those claims to the Copyright Holder in writing prior to submitting the
Contribution.

**(g) Awareness of Licensing Terms.**
You understand that the Project is governed by the FL Licence (see `LICENSE.txt`), and that the
Copyright Holder may, at their discretion, relicence the Work under a future version of the FL
Licence or under a different open-source licence. By submitting a Contribution, You acknowledge
and accept that Your Contribution may be redistributed under such future terms.

**(h) No Other Encumbrances.**
The Contribution is free from any lien, encumbrance, exclusive licence grant, or other restriction
that would prevent the Copyright Holder or any downstream recipient from exercising the rights
granted by the FL Licence.

**(i) Accuracy of Sign-off.**
The information contained in the `Signed-off-by` trailer is accurate. The name and email address
are those by which You can be contacted as the responsible contributor. You understand that this
record becomes a permanent part of the commit history and cannot be altered after the commit is
merged into the Project.

**(j) Public Record.**
You understand and consent to the fact that Your certification under this DCO, together with Your
name and email address, will be made publicly available as part of the commit history of the
Project.

---

## 5. How to Apply This DCO

To certify that a commit satisfies this DCO, add the following trailer **as the last line of the
commit message body**, preceded by a blank line:

```
Signed-off-by: Your Full Name <your.email@example.com>
```

The name and email address must match a real identity by which You can be reached. Pseudonyms,
aliases, or addresses that bounce are not acceptable.

### Example Commit Message

```
feat: add SIMD-accelerated find for AVX2-capable hosts

Implements a vectorised first-character scan using 256-bit AVX2
intrinsics when __AVX2__ is defined at compile time, falling back
to the scalar two-way search for other targets.

Benchmark results on Intel Core i9-13900K (AVX2 enabled):
  find (4 KB haystack)   1 187 ns  →  341 ns  (-71.3 %)
  find (64 KB haystack)  18 440 ns → 4 902 ns  (-73.4 %)

All four CTest tests pass. Zero compiler warnings with GCC 13
and Clang 17 under -Wall -Wextra -Wpedantic.

Signed-off-by: Contributor Name <contributor@example.com>
Co-Authored-By: Claude <noreply@anthropic.com>
```

### Git Configuration

To configure Git to include your identity automatically in all commits:

```bash
git config --global user.name  "Your Full Name"
git config --global user.email "your.email@example.com"
```

Some tools (e.g., `git commit -s`) can append the `Signed-off-by` trailer automatically. You are
encouraged to use such tooling to reduce the risk of omitting the trailer.

---

## 6. Retroactive Certification

If you have already committed work to the Project without a `Signed-off-by` trailer and wish to
certify those commits, contact the Copyright Holder with a written statement enumerating the commit
SHA identifiers and affirming that each commit satisfies the certification in Section 4 as of the
date of original submission. The Copyright Holder will record the retroactive certification in the
project's issue tracker or supplementary metadata.

Do **not** amend or rebase merged commits solely to add the trailer; doing so rewrites published
history and creates confusion for contributors who have branched from those commits.

---

## 7. Scope of Certification

The DCO certification applies:

- To every commit merged into the Project's canonical repository, including commits to feature
  branches, hotfix branches, release branches, and the default branch.
- To commits in pull requests, regardless of whether those pull requests are ultimately merged.
- To patches submitted by email or any other channel, treated as if they were commits.

The DCO certification does **not**:

- Constitute a warranty, representation, or guarantee that the Contribution is free of defects,
  bugs, security vulnerabilities, or errors.
- Grant any rights beyond those already granted by the FL Licence.
- Create any employment, agency, partnership, or joint-venture relationship between the Contributor
  and the Copyright Holder.

---

## 8. Relationship to the Contributor Licence Agreement

For corporate or entity contributors, or for individual contributors whose Contributions are
substantial in scope, the Copyright Holder may additionally require execution of the fl
Contributor Licence Agreement (`CLA.md`). The CLA provides additional representations, patent
grants, and governance provisions that complement but do not replace this DCO.

Where both a CLA and a DCO Sign-off are required:

- The CLA governs the legal relationship between the parties and provides the substantive licence
  grant.
- The DCO Sign-off on each individual commit serves as a per-commit provenance record, confirming
  that the specific Contribution was made in accordance with both the DCO and the CLA.

---

## 9. Enforcement

A pull request or patch that lacks a valid `Signed-off-by` trailer (or for which the trailer does
not match a real, contactable identity) will not be merged until the deficiency is remedied. The
Copyright Holder reserves the right to revert, remove, or quarantine any Contribution for which
the DCO certification is found to be inaccurate, incomplete, or made without authority, pending
investigation and resolution.

---

## 10. Attribution

This DCO was modelled in part on the Linux Kernel Developer's Certificate of Origin 1.1
(https://developercertificate.org), with substantial modifications to address the specific
governance, licensing, intellectual property, and employment provisions of the fl Project.

---

*End of Developer Certificate of Origin — fl (Forever Lightweight) v1.0*
