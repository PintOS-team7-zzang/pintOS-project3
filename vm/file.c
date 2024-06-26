/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
    .swap_in = file_backed_swap_in,
    .swap_out = file_backed_swap_out,
    .destroy = file_backed_destroy,
    .type = VM_FILE,
};

/* The initializer of file vm */
/* 파일 지원 페이지 하위 시스템을 초기화 */
void vm_file_init(void) {
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva) {
    /* Set up the handler */
    page->operations = &file_ops;

    struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool file_backed_swap_in(struct page *page, void *kva) {
    struct file_page *file_page UNUSED = &page->file;
    // 파일에서 콘텐츠를 읽어(read 함수 사용 ? ) kva 페이지에서 swap in합니다. 파일 시스템과 동기화해야 합니다.
    // file_read_at() 를 사용해서 kva 에 페이지를 올림
    struct aux *aux = (struct aux *)page->uninit.aux;  

    file_seek(aux->file,aux->ofs);
    off_t read_bytes = file_read(aux->file, kva , aux->page_read_bytes); // 읽은 바이트 수를 반환
    
    if ((int)read_bytes != (int)aux->page_read_bytes)
        return false;

    memset(kva + aux->page_read_bytes, 0, aux->page_zero_bytes); 

    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool file_backed_swap_out(struct page *page) {
    // victim의 페이지가 들어옴
    struct file_page *file_page UNUSED = &page->file;
    struct aux* aux = (struct aux *)page->uninit.aux; //file의 aux를 가져옴
    if(pml4_is_dirty(thread_current()->pml4, page->va)) // 먼저 페이지가 dirty 인지 확인
    {   
        // buffer(page->va)에 있는 데이터를 size만큼, file의 file_ofs부터 써줌
        file_write_at(aux->file , page->va, aux->page_read_bytes ,aux->ofs);  // 변경 사항을 파일에 다시 기록
        pml4_set_dirty(thread_current()->pml4, page->va, 0); // 변경 사항 다시 변경해줌

    }
    pml4_clear_page(thread_current()->pml4,page->va);  // present bit을 0으로 만들어서 디스크에 내려(swap out)있음

}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void file_backed_destroy(struct page *page) {
    struct file_page *file_page UNUSED = &page->file;
    struct aux* aux = (struct aux *)page->uninit.aux; //file의 aux를 가져옴
    if(pml4_is_dirty(thread_current()->pml4, page->va)) // 내용이 변경된 경우
    {   
        // buffer(page->va)에 있는 데이터를 size만큼, file의 file_ofs부터 써줌
        file_write_at(aux->file , page->va, aux->page_read_bytes ,aux->ofs);  // 변경 사항을 파일에 다시 기록
        pml4_set_dirty(thread_current()->pml4, page->va, 0); // 변경 사항 다시 변경해줌

    }
    pml4_clear_page(thread_current()->pml4,page->va);  // present bit을 0으로 만들어서 디스크에 내려(swap out)있음

}

/* Do the mmap */
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset) {
		// 파일의 오프셋바이트 부터 length 바이트까지 가상주소공간에 데이터를 올리기 위해 load
        int cnt;
       
        /* 사용자가 읽고싶은 영역의 길이 : length */
		size_t read_bytes = length > file_length(file) ? file_length(file) : length;
		size_t zero_bytes = PGSIZE - (read_bytes % PGSIZE);
        struct file *re_file = file_reopen(file); // 독립적인 파일을 갖기 위함

        // 연속된 페이지의 시작 주소 리턴
        void *start_addr = addr;

        if (read_bytes % PGSIZE == 0)
        {
            cnt = read_bytes / PGSIZE;
        } else
        {
            cnt = read_bytes / PGSIZE + 1;
        }
        

		while (read_bytes > 0 || zero_bytes > 0) {
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        // aux를 설정 후 lazy_load_segment에 정보를 제공
        struct aux *aux = (struct aux *)malloc(sizeof(struct aux));
        aux->file = re_file;
        aux->ofs = offset;
        aux->page_read_bytes = page_read_bytes;
        aux->writable = writable;
        aux->page_zero_bytes = page_zero_bytes;

        if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, aux))
            return false;
        
        struct page *page = spt_find_page(&thread_current()->spt, addr);
        page->page_cnt = cnt;
        cnt--;

        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }
    return start_addr;
}

/* Do the munmap */
void do_munmap(void *addr) {

    struct page * page = spt_find_page(&thread_current()->spt, addr); // 1. 매핑 해제할 시작주소로 페이지를 가져옴
    int page_cnt = page->page_cnt;
    for (int i  = 0; i < page_cnt; i++)
    {
        if (page)
            destroy(page);
        addr += PGSIZE;
        page = spt_find_page(&thread_current()->spt, addr);
    }
    
}
