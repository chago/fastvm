﻿
#include "vm.h"

int         dc_initialize(DisassemblyCache *dc, int min, int hashsize)
{
    int i;
    ParserContext *pos;
    dc->minimumresue = min;
    dc->mask = hashsize - 1;
    if (dc->mask & hashsize)
        vm_error("Bad windowsize[%d] for disassembly cache, must be power of 2", hashsize);

    dc->list = vm_mallocz(sizeof(ParserContext *) * dc->minimumresue);
    dc->nextfree = 0;
    dc->hashtable = vm_mallocz(sizeof(ParserContext *) * hashsize);
    for (i = 0; i < dc->minimumresue; ++i) {
        pos = ParserContext_new(dc->contxtcache);
        ParserContext_initialize(pos, 75, 20, dc->constspace);
        dc->list[i] = pos;
    }

    pos = dc->list[0];
    for (i = 0; i < hashsize; i++)
        dc->hashtable[i] = pos;

    return 0;
}

int         DisassemblyCache_new(DisassemblyCache *dc,
    ContextCache *ccache, AddrSpace *cspace, int cachesize, int windowsize)
{
    memset(dc, 0, sizeof(dc[0]));

    dc->contxtcache = ccache;
    dc->constspace = cspace;
    dc_initialize(dc, cachesize, windowsize);

    return 0;
}

void        DisassemblyCache_delete(DisassemblyCache *dc)
{
}

ParserContext*      DisassemblyCache_getParserContext(DisassemblyCache *d, Address *addr)
{
    int hashindex = addr->offset & d->mask;
    ParserContext *res = d->hashtable[hashindex];
    if (Address_equal(&res->addr, addr))
        return res;

    res = d->list[d->nextfree];
    d->nextfree += 1;
    if (d->nextfree >= d->minimumresue)
        d->nextfree = 0;

    res->addr = *addr;
    res->parsestate = uninitialized;
    d->hashtable[hashindex] = res;
    return res;
}

int         PcodeCacher_new(PcodeCacher *p)
{
    return 0;
}

void        PcodeCacher_delete(PcodeCacher *p)
{
}

static VarnodeData*     PcodeCacher_expandPool(PcodeCacher *p, int size)
{
    int     curmax = p->endpool - p->poolstart;
    int     cursize = p->curpool - p->poolstart;
    if ((cursize + size) <= curmax)
        return p->curpool;

    int increase = (cursize + size) - curmax;
    if (increase < 100)
        increase = 100;

    int newsize = curmax + increase;

    VarnodeData *newpool = vm_mallocz(sizeof(newpool[0]) * newsize);
    int i;
    for (i = 0; i < cursize; i++)
        newpool[i] = p->poolstart[i];

    for (i = 0; i < p->isused.len; i++) {
        PcodeData *pdata = p->isused.ptab[i];
        VarnodeData *outvar = pdata->outvar;
        if (outvar) {
            outvar = newpool + (outvar - p->poolstart);
            pdata->outvar = outvar;
        }

        VarnodeData *invar = pdata->invar;
        if (invar) {
            invar = newpool + (invar - p->poolstart);
            pdata->invar = invar;
        }
    }
    RelativeRecord *iter;
    mlist_for_each(p->label_refs, iter, node, i) {
        VarnodeData *ref = iter->dataptr;
        iter->dataptr = newpool + (ref - p->poolstart);
    }

    vm_free(p->poolstart);
    p->poolstart = newpool;
    p->curpool = newpool + (cursize + size);
    p->endpool = newpool + newsize;
    return newpool + cursize;
}

VarnodeData*    PcodeCacher_allocateVarnodes(PcodeCacher *p, int size)
{
    VarnodeData *newptr = p->curpool + size;
    if (newptr <= p->endpool) {
        VarnodeData *res = p->curpool;
        p->curpool = newptr;
        return res;
    }

    return PcodeCacher_expandPool(p, size);
}

PcodeData*      PcodeCacher_allocateInstruction(PcodeCacher *p)
{
    PcodeData *pdata = vm_mallocz(sizeof(PcodeData));
    dynarray_add(&p->isused, pdata);
    return pdata;
}

void            PcodeCacher_addLabelRef(PcodeCacher *p, VarnodeData *ptr)
{
    RelativeRecord *rr = vm_mallocz(sizeof(rr[0]));

    mlist_add(p->label_refs, rr, node);
    rr->dataptr = ptr;
    rr->calling_index = p->isused.len;
}

#define PCODE_MAGIC             0xbadbeef

void            PcodeCacher_addLabel(PcodeCacher *p, int id)
{
    while (p->labels.len <= id)
        dynarray_add(&p->labels, (void *)PCODE_MAGIC);

    p->labels.ptab[id] = (void *)p->isused.len;
}

void            PcodeCacher_clear(PcodeCacher *p)
{
    RelativeRecord *r;
    p->curpool = p->poolstart;
    dynarray_reset(&p->isused);
    while ((r = p->label_refs.list)) {
        mlist_del(p->label_refs, r, node);
        vm_free(r);
    }
    dynarray_reset(&p->labels);
}

void            PcodeCacher_resolveRelatives(PcodeCacher *p)
{
    RelativeRecord *iter;
    int i;
    mlist_for_each(p->label_refs, iter, node, i) {
        VarnodeData *ptr = iter->dataptr;
        intb id = ptr->offset;
        if ((id >= p->labels.len) || (p->labels.ptab[id] == (void *)PCODE_MAGIC))
            vm_error("Reference to non-existant sleigh label");

        intb res = (long)p->labels.ptab[id] - iter->calling_index;
        res &= calc_mask(ptr->size);
        ptr->offset = res;
    }
}

void            Sleigh_reset(VMState *vm)
{
}

void            Sleigh_registerContext(VMState *vm, const char *name, int startbit, int endbit)
{
}

void            Sleigh_reregisterContext(VMState *vm)
{
    SymbolScope *glb = SymbolTable_getGlobalScope(&vm->slgh.symtab);
    struct rb_node *iter;
    SleighSymbol *sym;

    for (iter = rb_first(&glb->tree); iter; iter = rb_next(iter)) {
        sym = scope_container_of(iter);
        if (sym->type == context_symbol) {
            ContextField *field = SleighSymbol_getPatternValue(sym);
        }
    }
}

void            Sleigh_initialize(VMState *vm)
{
}

void            Sleigh_resolve(VMState *vm, ParserContext *pos)
{
    vm->binload(vm, pos->buf, sizeof(pos->buf), &pos->addr);

    ParserWalker *walker = ParserWalker_new(pos);
    ParserContext_deallocateState(pos, walker);
    Constructor *ct, *subct;
    uint4 off;
    int oper, numoper;

    pos->delayslot = 0;
    ParserWalker_setOffset(walker, 0);
    ParserContext_clearCommits(pos);
    ParserContext_loadContext(pos);
    ct = SleighSymbol_resolve(vm->slgh.root, walker);
    walker->point->ct = ct;

    while (walker->point) {
        ct = walker->point->ct;
        oper = walker->breadcrumb[walker->depth];
        numoper = ct->operands.len;
        while (oper < numoper) {
            OperandSymbol *sym = ct->operands.ptab[oper];
            off = ParserWalker_getOffset(walker, sym->operand.offsetbase) + sym->operand.reloffset;
            walker->point->offset = off;
            TripleSymbol *tsym = sym->operand.triple;
            if (tsym) {
                subct = SleighSymbol_resolve(tsym, walker);
                if (subct) {
                    walker->point->ct = subct;
                    Constructor_applyContext(subct, walker);
                    break;
                }
            }

            walker->point->length = sym->operand.minimumlength;
            ParserWalker_popOperand(walker);
            oper += 1;
        }

        if (oper >= numoper) {
            ParserWalker_calcCurrentLength(walker, ct->minimumlength, numoper);
            ParserWalker_popOperand(walker);

            ConstructTpl *temp = ct->templ;
            if (temp && (temp->delayslot > 0))
                pos->delayslot = temp->delayslot;
        }
    }

    pos->naddr = pos->addr;
    Address_add(&pos->naddr, ParserContext_getLength(pos));
    pos->parsestate = disassembly;
}

void            Sleigh_resolveHandles(VMState *vm, ParserContext *pos)
{
}

ParserContext*  Sleigh_obtainContext(VMState *vm, Address *addr, int state)
{
    ParserContext *pos = DisassemblyCache_getParserContext(vm->discache, addr);
    int curstate = pos->parsestate;
    if (curstate >= state)
        return pos;

    if (curstate == uninitialized) {
        Sleigh_resolve(vm, pos);

        if (state == disassembly)
            return pos;
    }

    Sleigh_resolveHandles(vm, pos);

    return pos;
}

int     Sleigh_printAssembly(VMState *vm, Address *addr)
{
    ParserContext *pos = Sleigh_obtainContext(vm, addr, disassembly);
    ParserWalker *walker = ParserWalker_new(pos);
    ParserWalker_baseState(walker);

    Constructor *ct = ParserWalker_getConstructor(walker);
    CString cs = { 0 };
    Constructor_printMnemonic(ct, &cs, walker);
    cstr_ccat(&cs, ' ');
    Constructor_printBody(ct, &cs, walker);

    printf("[%s]\n", cs.data);

    return ParserContext_getLength(pos);
}

int     Sleigh_instructionLength(VMState *vm, Address *addr)
{
    return 0;
}