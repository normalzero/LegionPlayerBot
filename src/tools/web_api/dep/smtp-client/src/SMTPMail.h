/**
 * @file
 * @brief SMTPMail class wrapper for smtp-client library.
 * @author James Humphrey (mail@somnisoft.com)
 * @version 1.00
 *
 * Thin CPP wrapper class around the smtp-client C library.
 *
 * This software has been placed into the public domain using CC0.
 */
#ifndef SMTP_MAIL_H
#define SMTP_MAIL_H

#include <exception>

#include "smtp.h"

/**
 * @class SMTPMailException
 *
 * This exception will get thrown whenever an SMTP client function fails.
 */
class SMTPMailException : public std::exception{
public:
  /**
   * Create the exception with the corresponding @p status_code.
   *
   * @param[in] status_code An error status code returned from a previous call
   *                        to an smtp-client library function.
   */
  SMTPMailException(enum smtp_status_code status_code);

  /**
   * Get a description of the smtp-client status code that caused
   * this exception.
   *
   * @return Null-terminated string describing the smtp-client status code.
   */
  virtual const char* what() const noexcept;

private:
  /**
   * The status code generated by any of the smtp-client library functions.
   */
  enum smtp_status_code status_code;
};

/**
 * @class SMTPMail
 *
 * Thin CPP wrapper class over the smtp-client C library.
 */
class SMTPMail{
public:
  /**
   * Create the @ref SMTPMail object.
   */
  SMTPMail(void);

  /**
   * Free the @ref SMTPMail object.
   */
  ~SMTPMail(void);

  /**
   * Open a connection to an SMTP server.
   *
   * See @ref smtp_open.
   *
   * @param[in]  server              Server name or IP address.
   * @param[in]  port                Server port number.
   * @param[in]  connection_security See @ref smtp_connection_security.
   * @param[in]  flags               See @ref smtp_flag.
   * @param[in]  cafile              Path to certificate file, or NULL to use
   *                                 certificates in the default path.
   */
  void open(const char *const server,
            const char *const port,
            enum smtp_connection_security connection_security,
            enum smtp_flag flags,
            const char *const cafile);

  /**
   * Authenticate the user using one of the methods listed in
   * @ref smtp_authentication_method.
   *
   * See @ref smtp_auth.
   *
   * @param[in] auth_method See @ref smtp_authentication_method.
   * @param[in] user        Server authentication user name.
   * @param[in] pass        Server authentication user password.
   */
  void auth(enum smtp_authentication_method auth_method,
            const char *const user,
            const char *const pass);

  /**
   * Sends an email using the addresses, attachments, and headers defined
   * in the current SMTP context.
   *
   * See @ref smtp_mail.
   *
   * @param[in] body Null-terminated string to send in the email body.
   */
  void mail(const char *const body);

  /**
   * Close the SMTP connection and frees all resources held by the
   * SMTP context.
   *
   * See @ref smtp_close.
   */
  void close(void);

  /**
   * Get the current status/error code described in @ref smtp_status_code.
   *
   * See @ref smtp_status_code_get.
   *
   * @return @ref smtp_status_code.
   */
  int status_code_get(void);

  /**
   * Set the error status of the SMTP client context.
   *
   * See @ref smtp_status_code_set.
   *
   * @param[in] new_status_code See @ref smtp_status_code.
   */
  void status_code_set(enum smtp_status_code new_status_code);

  /**
   * Add a key/value header to the header list in the SMTP context.
   *
   * See @ref smtp_header_add.
   *
   * @param[in] key   Key name for new header. It must consist only of
   *                  printable US-ASCII characters except colon.
   * @param[in] value Value for new header. It must consist only of printable
   *                  US-ASCII, space, or horizontal tab. If set to NULL,
   *                  this will prevent the header from printing out.
   */
  void header_add(const char *const key,
                  const char *const value);

  /**
   * Free all memory related to email headers.
   *
   * See @ref smtp_header_clear_all.
   */
  void header_clear_all(void);

  /**
   * Add a FROM, TO, CC, or BCC address destination to this SMTP context.
   *
   * See @ref smtp_address_add.
   *
   * @param[in] type  See @ref smtp_address_type.
   * @param[in] email The email address of the party. Must consist only of
   *                  printable characters excluding the angle brackets
   *                  (<) and (>).
   * @param[in] name  Name or description of the party. Must consist only of
   *                  printable characters, excluding the quote characters. If
   *                  set to NULL, no name will get associated with this email.
   */
  void address_add(enum smtp_address_type type,
                   const char *const email,
                   const char *const name);

  /**
   * Free all memory related to the address list.
   *
   * See @ref smtp_address_clear_all.
   */
  void address_clear_all(void);

  /**
   * Add a file attachment from a path.
   *
   * See @ref smtp_attachment_add_mem.
   *
   * @param[in] name Filename of the attachment shown to recipients.
   * @param[in] path Path of file location to read from.
   */
  void attachment_add_path(const char *const name,
                           const char *const path);

  /**
   * Add an attachment using a file pointer.
   *
   * See @ref smtp_attachment_add_fp.
   *
   * @param[in] name Filename of the attachment shown to recipients.
   * @param[in] fp   File pointer already opened by the caller.
   */
  void attachment_add_fp(const char *const name,
                         FILE *fp);

  /**
   * Add an attachment with the data pulled from memory.
   *
   * See @ref smtp_attachment_add_mem.
   *
   * @param[in] name   Filename of the attachment shown to recipients. Must
   *                   consist only of printable characters excluding the
   *                   quote characters (') and ("), or the space character
   *                   ( ).
   * @param[in] data   Raw attachment data stored in memory.
   * @param[in] datasz Number of bytes in @p data, or -1 if data
   *                   null-terminated.
   */
  void attachment_add_mem(const char *const name,
                          const void *const data,
                          size_t datasz);

  /**
   * Remove all attachments from the SMTP client context.
   *
   * See @ref smtp_attachment_clear_all.
   */
  void attachment_clear_all(void);

private:
  /**
   * SMTP client context.
   */
  struct smtp *smtp;

  /**
   * Store the last smtp-client library function return code.
   */
  enum smtp_status_code rc;

  /**
   * Throw @ref SMTPMailException if the last smtp-client library function
   * failed.
   */
  void throw_bad_status_code(void);
};

#endif /* SMTP_MAIL_H */

